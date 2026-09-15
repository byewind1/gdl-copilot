#include "EvaluateLibraryPartCommand.hpp"

#include "APIdefs_Elements.h"

#include <cmath>
#include <iterator>
#include <string>
#include <vector>

// ============================================================================
// EvaluateLibraryPart 命令实现
//
// 流程：定位图库物件 → 读默认参数并应用覆盖值 → 在独立 undo scope 内临时
// 放置 → ACAPI_ModelAccess_Get3DInfo/GetComponent 遍历 body → 逐 pgon
// DecomposePgon 出凸子多边形后扇形三角化 → 删除临时元素。
// ============================================================================

namespace {

constexpr const char* CommandNamespaceStr = "OpenBrep";

struct Line2D {
	API_Coord from;
	API_Coord to;
};

struct Polygon2D {
	std::vector<API_Coord> points;
	bool filled = false;
};

struct Arc2D {
	API_Coord center;
	double radius = 0.0;
	double startAngle = 0.0;
	double endAngle = 0.0;
	bool whole = false;
};

struct Text2D {
	API_Coord location;
	GS::UniString text;
	double size = 0.0;
};

struct PrimitiveCollector {
	std::vector<Line2D> lines;
	std::vector<Polygon2D> polygons;
	std::vector<Arc2D> arcs;
	std::vector<Text2D> texts;
	Int32 unsupportedCount = 0;
	Int32 approximatedCurveCount = 0;
};

thread_local PrimitiveCollector* activePrimitiveCollector = nullptr;

GSErrCode CollectPrimitive (const API_PrimElement* primitive,
							 const void* par1,
							 const void* par2,
							 const void* /*par3*/)
{
	if (activePrimitiveCollector == nullptr || primitive == nullptr)
		return NoError;

	PrimitiveCollector& collector = *activePrimitiveCollector;
	switch (primitive->header.typeID) {
		case API_PrimLineID:
			collector.lines.push_back ({ primitive->line.c1, primitive->line.c2 });
			break;
		case API_PrimPLineID: {
			const API_Coord* coords = static_cast<const API_Coord*> (par1);
			if (coords == nullptr)
				break;
			for (Int32 i = 1; i < primitive->pline.nCoords; ++i)
				collector.lines.push_back ({ coords[i], coords[i + 1] });
			collector.approximatedCurveCount += primitive->pline.nArcs;
			break;
		}
		case API_PrimTriID: {
			Polygon2D polygon;
			polygon.filled = primitive->tri.solid;
			polygon.points.assign (std::begin (primitive->tri.c), std::end (primitive->tri.c));
			collector.polygons.push_back (std::move (polygon));
			break;
		}
		case API_PrimPolyID: {
			const API_Coord* coords = static_cast<const API_Coord*> (par1);
			const Int32* ends = static_cast<const Int32*> (par2);
			if (coords == nullptr || primitive->poly.nCoords <= 0)
				break;
			Int32 first = 1;
			const Int32 subPolygonCount = ends == nullptr ? 1 : primitive->poly.nSubPolys;
			for (Int32 sub = 1; sub <= subPolygonCount; ++sub) {
				const Int32 last = ends == nullptr ? primitive->poly.nCoords : ends[sub];
				if (last < first || last > primitive->poly.nCoords) {
					collector.unsupportedCount++;
					break;
				}
				Polygon2D polygon;
				polygon.filled = primitive->poly.solid;
				for (Int32 i = first; i <= last; ++i)
					polygon.points.push_back (coords[i]);
				collector.polygons.push_back (std::move (polygon));
				first = last + 1;
			}
			collector.approximatedCurveCount += primitive->poly.nArcs;
			break;
		}
		case API_PrimArcID:
			if (std::abs (primitive->arc.ratio - 1.0) < 1.0e-9) {
				collector.arcs.push_back ({ primitive->arc.orig, primitive->arc.r,
					primitive->arc.begAng, primitive->arc.endAng, primitive->arc.whole });
			} else {
				collector.unsupportedCount++; // 当前前端 payload 无椭圆弧表示
			}
			break;
		case API_PrimTextID: {
			GS::UniString content;
			if (par2 != nullptr)
				content = GS::UniString (reinterpret_cast<const GS::UniChar::Layout*> (par2));
			collector.texts.push_back ({ primitive->text.loc, content, primitive->text.heightMM / 1000.0 });
			break;
		}
		case API_PrimPointID:
		case API_PrimPictID:
			collector.unsupportedCount++;
			break;
		default:
			break; // control records are not drawable primitives
	}
	return NoError;
}

bool Wants (const GS::Array<GS::String>& wanted, const GS::String& value)
{
	if (wanted.IsEmpty ())
		return true;
	for (const GS::String& item : wanted) {
		if (item == value)
			return true;
	}
	return false;
}

GS::ObjectState MakeError (const GS::UniString& message)
{
	GS::ObjectState response;
	response.Add ("success", false);
	response.Add ("errorMessage", message);
	return response;
}

// 3×4 行主变换矩阵应用（API_Tranmat 注释里的公式）
API_Coord3D ApplyTranmat (const API_Tranmat& t, double x, double y, double z)
{
	return API_Coord3D {
		t.tmx[0] * x + t.tmx[1] * y + t.tmx[2] * z + t.tmx[3],
		t.tmx[4] * x + t.tmx[5] * y + t.tmx[6] * z + t.tmx[7],
		t.tmx[8] * x + t.tmx[9] * y + t.tmx[10] * z + t.tmx[11]
	};
}

bool NameMatches (const char* paramName, const GS::UniString& wanted)
{
	return GS::UniString (paramName).Compare (wanted, GS::CaseInsensitive) == GS::UniString::Equal;
}

// typeID is the legacy file category; modern windows can report Object.
// Ask Archicad's subtype search and match the resolved index, never the name alone.
bool BelongsToSubtype (const API_LibPart& part, API_LibTypeID subtype)
{
	API_LibPart ancestor = {};
	ancestor.typeID = subtype;
	API_LibPart matches[50] = {};
	Int32 count = 0;
	const GSErrCode err = ACAPI_LibraryPart_PatternSearch (
		&ancestor, GS::UniString ("\"") + GS::UniString (part.docu_UName) + "\"", matches, &count);
	bool found = false;
	for (Int32 i = 0; i < count && i < 50; ++i) {
		found = found || (err == NoError && matches[i].index == part.index);
		delete matches[i].location;
	}
	return found;
}

// 把 JSON 覆盖值应用到 API_AddParType 数组（数组参数跳过并记录名字）
void ApplyParameterOverrides (API_AddParType** addPars, Int32 addParNum,
							  const GS::ObjectState& overrides,
							  GS::Array<GS::UniString>& appliedNames,
							  GS::Array<GS::UniString>& skippedNames)
{
	const GS::HashSet<GS::String> fieldNames = overrides.GetFieldNames ();
	for (const GS::String& fieldName : fieldNames) {
		const GS::UniString wanted (fieldName);
		// A/B 是 API_ObjectType 的 xRatio/yRatio，不是普通 addPar。
		// Execute 使用字面字段名单独处理，这里不得再将其记为 skipped。
		if (wanted.Compare ("A", GS::CaseInsensitive) == GS::UniString::Equal ||
			wanted.Compare ("B", GS::CaseInsensitive) == GS::UniString::Equal)
			continue;
		bool found = false;
		for (Int32 i = 0; i < addParNum; ++i) {
			API_AddParType& par = (*addPars)[i];
			if (!NameMatches (par.name, wanted))
				continue;
			found = true;
			if (par.dim1 > 1 || par.dim2 > 1) {
				skippedNames.Push (wanted);   // 数组参数 v1 不支持覆盖
				break;
			}
			double numValue = 0.0;
			bool boolValue = false;
			GS::UniString strValue;
			if (overrides.Get (fieldName, boolValue) && par.typeID == APIParT_Boolean) {
				par.value.real = boolValue ? 1.0 : 0.0;
				appliedNames.Push (wanted);
			} else if (overrides.Get (fieldName, numValue) &&
					   par.typeID != APIParT_CString &&
					   par.typeID != APIParT_Separator &&
					   par.typeID != APIParT_Title &&
					   par.typeID != APIParT_Dictionary) {
				par.value.real = numValue;
				appliedNames.Push (wanted);
			} else if (overrides.Get (fieldName, strValue)) {
				if (par.typeID == APIParT_CString) {
					GS::ucscpy (par.value.uStr, strValue.ToUStr ());
					appliedNames.Push (wanted);
				} else {
					skippedNames.Push (wanted);   // 类型不匹配
				}
			} else {
				skippedNames.Push (wanted);   // JSON 类型不受支持或与参数类型不匹配
			}
			break;
		}
		if (!found)
			skippedNames.Push (wanted);
	}
}

} // unnamed namespace

GS::String EvaluateLibraryPartCommand::GetName () const
{
	return "EvaluateLibraryPart";
}

GS::String EvaluateLibraryPartCommand::GetNamespace () const
{
	return CommandNamespaceStr;
}

GS::Optional<GS::UniString> EvaluateLibraryPartCommand::GetSchemaDefinitions () const
{
	return {};
}

GS::Optional<GS::UniString> EvaluateLibraryPartCommand::GetInputParametersSchema () const
{
	return R"({
		"type": "object",
		"properties": {
			"libPartName": { "type": "string" },
			"libPartGuid": { "type": "string" },
			"parameters": { "type": "object" },
			"want": {
				"type": "array",
				"items": { "type": "string", "enum": ["mesh3d", "prims2d"] }
			}
		},
		"additionalProperties": false
	})";
}

GS::Optional<GS::UniString> EvaluateLibraryPartCommand::GetResponseSchema () const
{
	return {};
}

API_AddOnCommandExecutionPolicy EvaluateLibraryPartCommand::GetExecutionPolicy () const
{
	return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread;
}

bool EvaluateLibraryPartCommand::IsProcessWindowVisible () const
{
	return false;
}

void EvaluateLibraryPartCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}

GS::ObjectState EvaluateLibraryPartCommand::Execute (const GS::ObjectState& parameters,
													 GS::ProcessControl& /*processControl*/) const
{
	// ── 1. 解析入参 ─────────────────────────────────────────────────
	GS::UniString libPartName;
	GS::UniString libPartGuid;
	parameters.Get ("libPartName", libPartName);
	parameters.Get ("libPartGuid", libPartGuid);
	GS::ObjectState overrides;
	parameters.Get ("parameters", overrides);
	GS::Array<GS::String> wanted;
	parameters.Get ("want", wanted);
	const bool wantsMesh3D = Wants (wanted, "mesh3d");
	const bool wantsPrimitives2D = Wants (wanted, "prims2d");
	if (!wantsMesh3D && !wantsPrimitives2D)
		return MakeError ("want 至少需要包含 mesh3d 或 prims2d");

	if (libPartName.IsEmpty () && libPartGuid.IsEmpty ())
		return MakeError ("libPartName 或 libPartGuid 必须提供一个");

	// ── 2. 定位图库物件 ─────────────────────────────────────────────
	API_LibPart libPart = {};
	if (!libPartGuid.IsEmpty ()) {
		CHTruncate (libPartGuid.ToCStr (), libPart.ownUnID, sizeof (libPart.ownUnID));
	} else {
		GS::ucscpy (libPart.docu_UName, libPartName.ToUStr ());
	}
	if (ACAPI_LibraryPart_Search (&libPart, false) != NoError)
		return MakeError ("图库中找不到物件: " + (libPartName.IsEmpty () ? libPartGuid : libPartName));
	delete libPart.location;
	libPart.location = nullptr;

	const bool isWindow = BelongsToSubtype (libPart, APILib_WindowID);
	const bool isDoor = !isWindow && BelongsToSubtype (libPart, APILib_DoorID);
	const bool needsWall = isWindow || isDoor;
	if (BelongsToSubtype (libPart, APILib_SkylightID))
		return MakeError ("天窗需要屋顶宿主，当前版本暂不支持权威求值");
	if (!needsWall && libPart.typeID != APILib_ObjectID && libPart.typeID != APILib_LampID)
		return MakeError ("该物件类型暂不支持权威求值（仅支持 Object/Lamp）");

	// ── 3. 读默认参数并应用覆盖 ─────────────────────────────────────
	double dummyA = 0.0, dummyB = 0.0;
	Int32 addParNum = 0;
	API_AddParType** addPars = nullptr;
	if (ACAPI_LibraryPart_GetParams (libPart.index, &dummyA, &dummyB, &addParNum, &addPars) != NoError)
		return MakeError ("读取物件默认参数失败");

	GS::Array<GS::UniString> appliedParams;
	GS::Array<GS::UniString> skippedParams;
	double requestedDimension = 0.0;
	if (overrides.Get ("A", requestedDimension)) {
		dummyA = requestedDimension;
		appliedParams.Push ("A");
	}
	if (overrides.Get ("B", requestedDimension)) {
		dummyB = requestedDimension;
		appliedParams.Push ("B");
	}
	ApplyParameterOverrides (addPars, addParNum, overrides, appliedParams, skippedParams);
	// A/B 是元素尺寸字段，不只是一对普通附加参数。覆盖存在时同步到
	// xRatio/yRatio，否则临时实例仍会按图库默认尺寸求值。

	// ── 4. 组装元素 + 参数 memo ─────────────────────────────────────
	API_Element element = {};
	element.header.type = isWindow ? API_WindowID : isDoor ? API_DoorID :
		(libPart.typeID == APILib_LampID ? API_LampID : API_ObjectID);
	API_ElementMemo memo = {};
	GSErrCode defaultsErr = ACAPI_Element_GetDefaults (&element, &memo);
	if (defaultsErr != NoError) {
		ACAPI_DisposeAddParHdl (&addPars);
		return MakeError ("读取元素默认值失败: " + GS::UniString::Printf ("%d", defaultsErr));
	}
	ACAPI_DisposeAddParHdl (&memo.params);
	memo.params = addPars;
	if (needsWall) {
		element.window.openingBase.libInd = libPart.index;
		element.window.openingBase.width = dummyA;
		element.window.openingBase.height = dummyB;
		element.window.lower = 0.0;
		element.window.objLoc = (dummyA + 2.0) / 2.0;
	} else {
		element.object.pos = API_Coord { 0.0, 0.0 };
		element.object.level = 0.0;
		element.object.xRatio = dummyA;
		element.object.yRatio = dummyB;
		element.object.useXYFixSize = true;
		element.object.libInd = libPart.index;
	}

	GS::ObjectState response;
	GS::UniString innerError;
	PrimitiveCollector primitives;
	Int32 invalidFaceCount = 0;

	GSErrCode err = ACAPI_CallUndoableCommand ("OpenBrep Evaluate Library Part",
		[&]() -> GSErrCode {
			API_Guid hostGuid = APINULLGuid;
			if (needsWall) {
				API_Element wall = {};
				wall.header.type = API_WallID;
				GSErrCode hostErr = ACAPI_Element_GetDefaults (&wall, nullptr);
				if (hostErr == NoError) {
					wall.wall.begC = {0.0, 0.0};
					wall.wall.endC = {dummyA + 2.0, 0.0};
					wall.wall.angle = 0.0;
					wall.wall.bottomOffset = 0.0;
					wall.wall.relativeTopStory = 0;
					wall.wall.height = dummyB + 1.0;
					hostErr = ACAPI_Element_Create (&wall, nullptr);
				}
				if (hostErr != NoError) {
					innerError = "创建临时宿主墙失败: " + GS::UniString::Printf ("%d", hostErr);
					return hostErr;
				}
				hostGuid = wall.header.guid;
				element.window.owner = hostGuid;
			}
			// 临时放置
			GSErrCode createErr = ACAPI_Element_Create (&element, &memo);
			if (createErr != NoError) {
				innerError = "临时放置物件失败: " + GS::UniString::Printf ("%d", createErr);
				return createErr;
			}
			const API_Guid tempGuid = element.header.guid;

			GSErrCode evaluationErr = NoError;

			// 让 Archicad 真实引擎生成 3D（无需打开 3D 窗口，官方文档保证）
			API_ElemInfo3D info3D = {};
			const GSErrCode infoErr = wantsMesh3D
				? ACAPI_ModelAccess_Get3DInfo (element.header, &info3D)
				: NoError;
			if (wantsMesh3D && infoErr == NoError) {
				const auto& meshList = response.AddList<GS::ObjectState> ("meshes");
				for (Int32 iBody = info3D.fbody; iBody <= info3D.lbody; ++iBody) {
					API_Component3D comp = {};
					comp.header.typeID = API_BodyID;
					comp.header.index = iBody;
					if (ACAPI_ModelAccess_GetComponent (&comp) != NoError)
						continue;   // 含 APIERR_DELETED（影子/已删 body）

					const API_BodyType& body = comp.body;
					GS::ObjectState meshOs;
					meshOs.Add ("name", GS::UniString::Printf ("body_%d", iBody));

					// 材质（body 级；iumat 为全局 UMAT 索引）
					Int32 iumat = body.iumat < 0 ? -body.iumat : body.iumat;
					if (iumat > 0) {
						API_Component3D matComp = {};
						matComp.header.typeID = API_UmatID;
						matComp.header.index = iumat;
						if (ACAPI_ModelAccess_GetComponent (&matComp) == NoError) {
							GS::ObjectState colorOs;
							colorOs.Add ("red", matComp.umat.mater.surfaceRGB.f_red);
							colorOs.Add ("green", matComp.umat.mater.surfaceRGB.f_green);
							colorOs.Add ("blue", matComp.umat.mater.surfaceRGB.f_blue);
							meshOs.Add ("color", colorOs);
						}
					}

					// 顶点（局部坐标 × body 变换矩阵 → 世界坐标）
					const auto& vertexList = meshOs.AddList<double> ("vertices");
					for (Int32 iv = 1; iv <= body.nVert; ++iv) {
						API_Component3D vComp = {};
						vComp.header.typeID = API_VertID;
						vComp.header.index = iv;
						if (ACAPI_ModelAccess_GetComponent (&vComp) != NoError) {
							vertexList (0.0); vertexList (0.0); vertexList (0.0);
							continue;
						}
						const API_Coord3D w = ApplyTranmat (body.tranmat, vComp.vert.x, vComp.vert.y, vComp.vert.z);
						vertexList (w.x); vertexList (w.y); vertexList (w.z);
					}

					// 面：逐 pgon 凸分解（官方接口处理凹多边形与洞）→ 扇形三角化
					const auto& faceList = meshOs.AddList<Int32> ("faces");
					for (Int32 ip = 1; ip <= body.nPgon; ++ip) {
						API_Component3D pComp = {};
						pComp.header.typeID = API_PgonID;
						pComp.header.index = ip;
						if (ACAPI_ModelAccess_GetComponent (&pComp) != NoError)
							continue;
						if ((pComp.pgon.status & APIPgon_Invis) != 0)
							continue;

						Int32** cpoly = nullptr;
						if (ACAPI_ModelAccess_DecomposePgon (ip, &cpoly) != NoError || cpoly == nullptr || *cpoly == nullptr)
							continue;
						Int32 pos = 0;
						const Int32 nSub = -(*cpoly)[pos++];
						for (Int32 s = 0; s < nSub; ++s) {
							const Int32 m = -(*cpoly)[pos++];
							std::vector<Int32> loop;
							bool valid = m >= 3;
							for (Int32 k = 0; k < m; ++k) {
								const Int32 rawIndex = (*cpoly)[pos++];
								if (rawIndex <= 0 || rawIndex > body.nVert)
									valid = false;
								loop.push_back (rawIndex - 1);   // 官方 VERT 索引为 1-based
							}
							if (valid) {
								for (size_t k = 1; k + 1 < loop.size (); ++k) {
									faceList (loop[0]);
									faceList (loop[k]);
									faceList (loop[k + 1]);
								}
							} else {
								invalidFaceCount++;
							}
						}
						BMKillHandle (reinterpret_cast<GSHandle*> (&cpoly));
					}

					meshList (meshOs);
				}

				// 包围盒
				GS::ObjectState boundsOs;
				boundsOs.Add ("xMin", info3D.bounds.xMin);
				boundsOs.Add ("yMin", info3D.bounds.yMin);
				boundsOs.Add ("zMin", info3D.bounds.zMin);
				boundsOs.Add ("xMax", info3D.bounds.xMax);
				boundsOs.Add ("yMax", info3D.bounds.yMax);
				boundsOs.Add ("zMax", info3D.bounds.zMax);
				response.Add ("bounds", boundsOs);
			} else if (wantsMesh3D) {
				innerError = "Archicad 3D 求值失败: " + GS::UniString::Printf ("%d", infoErr);
				evaluationErr = infoErr;
			}

			// ShapePrims 会切换 ModelAccess 的活动上下文，必须放在 3D
			// body 遍历之后；用已放置实例 GUID 保证覆盖参数一致。
			if (evaluationErr == NoError && wantsPrimitives2D) {
				activePrimitiveCollector = &primitives;
				const GSErrCode primErr = ACAPI_LibraryPart_ShapePrims (
					libPart.index, tempGuid, APIGdl_FromFloor, CollectPrimitive);
				activePrimitiveCollector = nullptr;
				if (primErr != NoError) {
					innerError = "Archicad 2D 求值失败: " + GS::UniString::Printf ("%d", primErr);
					evaluationErr = primErr;
				}
			}

			// 删除临时元素（undo scope 内创建+删除，用户项目与撤销栈无残留）
			GS::Array<API_Guid> toDelete;
			toDelete.Push (tempGuid);
			if (hostGuid != APINULLGuid) toDelete.Push (hostGuid);
			const GSErrCode deleteErr = ACAPI_Element_Delete (toDelete);
			if (deleteErr != NoError) {
				// 返回错误会让整个 undoable command 回滚，确保即使显式删除失败
				// 也不会把临时实例提交进用户项目。
				innerError = "清理临时物件失败，操作已回滚: " + GS::UniString::Printf ("%d", deleteErr);
				return deleteErr;
			}
			// 成功时也主动回滚整个事务：响应数据在 lambda 外部，
			// 而项目修改和 undo 记录不能提交。显式删除仍作为双保险。
			return evaluationErr == NoError ? APIERR_CANCEL : evaluationErr;
		});

	ACAPI_DisposeElemMemoHdls (&memo);

	if (err != APIERR_CANCEL)
		return MakeError (innerError.IsEmpty () ? "求值失败" : innerError);

	response.Add ("success", true);
	response.Add ("libPartName", libPartName);
	if (wantsPrimitives2D) {
		GS::ObjectState preview2D;
		const auto& lines = preview2D.AddList<GS::ObjectState> ("lines");
		for (const Line2D& line : primitives.lines) {
			GS::ObjectState item;
			const auto& from = item.AddList<double> ("from");
			from (line.from.x); from (line.from.y);
			const auto& to = item.AddList<double> ("to");
			to (line.to.x); to (line.to.y);
			lines (item);
		}
		const auto& polygons = preview2D.AddList<GS::ObjectState> ("polygons");
		for (const Polygon2D& polygon : primitives.polygons) {
			GS::ObjectState item;
			const auto& points = item.AddList<double> ("points");
			for (const API_Coord& point : polygon.points) {
				points (point.x); points (point.y);
			}
			item.Add ("filled", polygon.filled);
			polygons (item);
		}
		const auto& arcs = preview2D.AddList<GS::ObjectState> ("arcs");
		for (const Arc2D& arc : primitives.arcs) {
			GS::ObjectState item;
			item.Add ("cx", arc.center.x);
			item.Add ("cy", arc.center.y);
			item.Add ("r", arc.radius);
			item.Add ("a0", arc.startAngle * 180.0 / M_PI);
			item.Add ("a1", arc.endAngle * 180.0 / M_PI);
			item.Add ("whole", arc.whole);
			arcs (item);
		}
		const auto& texts = preview2D.AddList<GS::ObjectState> ("texts");
		for (const Text2D& text : primitives.texts) {
			GS::ObjectState item;
			item.Add ("x", text.location.x);
			item.Add ("y", text.location.y);
			item.Add ("text", text.text);
			item.Add ("size", text.size);
			texts (item);
		}
		preview2D.Add ("unsupportedCount", primitives.unsupportedCount);
		preview2D.Add ("approximatedCurveCount", primitives.approximatedCurveCount);
		response.Add ("preview2d", preview2D);
	}
	if (invalidFaceCount > 0)
		response.Add ("invalidFaceCount", invalidFaceCount);
	if (!appliedParams.IsEmpty ()) {
		const auto& list = response.AddList<GS::UniString> ("appliedParameters");
		for (const GS::UniString& n : appliedParams)
			list (n);
	}
	if (!skippedParams.IsEmpty ()) {
		const auto& list = response.AddList<GS::UniString> ("skippedParameters");
		for (const GS::UniString& n : skippedParams)
			list (n);
	}
	return response;
}
