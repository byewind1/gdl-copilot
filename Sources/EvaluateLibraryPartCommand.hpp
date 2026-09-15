#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "ObjectState.hpp"

// OpenBrep 权威预览命令：让后台 Archicad 用真实引擎求值一个图库物件
// （当前参数 + 覆盖参数），返回 3D mesh（顶点/三角面/材质）供工作台
// three.js 视口渲染。机制：官方 Add-On Commands（同 Tapir）。
// 副作用纪律：物件在独立 undo scope 内临时放置，读完 3D 后立即删除——
// 用户项目与撤销栈无残留。
class EvaluateLibraryPartCommand : public API_AddOnCommand {
public:
	EvaluateLibraryPartCommand () = default;

	GS::String									GetName () const override;
	GS::String									GetNamespace () const override;
	GS::Optional<GS::UniString>					GetSchemaDefinitions () const override;
	GS::Optional<GS::UniString>					GetInputParametersSchema () const override;
	GS::Optional<GS::UniString>					GetResponseSchema () const override;
	API_AddOnCommandExecutionPolicy				GetExecutionPolicy () const override;
	bool										IsProcessWindowVisible () const override;
	GS::ObjectState								Execute (const GS::ObjectState& parameters,
														 GS::ProcessControl& processControl) const override;
	void										OnResponseValidationFailed (const GS::ObjectState& response) const override;
};
