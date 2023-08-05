#include "SharedDefinitions.h"
#include "SomeMacros.h"
#include "StringUtils.h"

#include "../vendor/imgui.cpp"
#include "../vendor/imgui_demo.cpp"
#include "../vendor/imgui_draw.cpp"
#include "../vendor/imgui_tables.cpp"
#include "../vendor/imgui_widgets.cpp"

#include "CrazyLog.cpp"
#include "ConsolaTTF.cpp"



struct AppMemory {
	CrazyLog Log;
};



void AppUpdate(float DeltaTime, PlatformContext* pPlatformCtx)
{
	// TODO(matiasp): Make a function that return the size required from the app.
	AppMemory *pMem = (AppMemory *)pPlatformCtx->pPermanentMemory;
	
#if 0
	bool bShowDemo = true;
	ImGui::ShowDemoWindow(&bShowDemo);
#endif 
	
#if 1
	bool Open = true;
	
	// For the demo: add a debug button _BEFORE_ the normal log window contents
	// We take advantage of a rarely used feature: multiple calls to Begin()/End() are appending to the _same_ window.
	// Most of the contents of the window will be added by the log.Draw() call.RECT ClientRect;	
	ImGuiViewport* pViewPort = ImGui::GetMainViewport();
		
	ImGui::SetNextWindowPos(pViewPort->WorkPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(pViewPort->WorkSize, ImGuiCond_Always);
	
	pMem->Log.Draw(DeltaTime, pPlatformCtx, "Crazy Log", &Open);
	
#endif
}

void AddEmbeddedConsolaTTF() {
	ImFontConfig font_cfg = ImFontConfig();
	font_cfg.OversampleH = font_cfg.OversampleV = 1;
	font_cfg.PixelSnapH = true;
	
	float FontSize = 16.f;
	
	if (font_cfg.SizePixels <= 0.0f)
		font_cfg.SizePixels = FontSize * 1.0f;
	if (font_cfg.Name[0] == '\0')
		ImFormatString(font_cfg.Name, IM_ARRAYSIZE(font_cfg.Name), "Consola.ttf, %dpx", (int)font_cfg.SizePixels);
	font_cfg.EllipsisChar = (ImWchar)0x0085;
	font_cfg.GlyphOffset.y = 1.0f * IM_FLOOR(font_cfg.SizePixels / FontSize);  

	const char* ttf_compressed_base85 = ConsolaTTF_compressed_data_base85;
	const ImWchar* glyph_ranges = ImGui::GetIO().Fonts->GetGlyphRangesDefault();
	ImGui::GetIO().Fonts->AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85, font_cfg.SizePixels, &font_cfg, glyph_ranges);
}

void AppInit(PlatformContext* pPlatformCtx, PlatformReloadContext* pPlatformReloadCtx) 
{
	ImGui::SetCurrentContext(pPlatformReloadCtx->pImGuiCtx);
	ImGui::SetAllocatorFunctions(pPlatformReloadCtx->pImGuiAllocFunc, pPlatformReloadCtx->pImGuiFreeFunc);
	

	ImGui::GetIO().Fonts->AddFontDefault();
	AddEmbeddedConsolaTTF();
	
	ASSERT(sizeof(AppMemory) <= pPlatformCtx->PermanentMemoryCapacity);
	AppMemory *pMem = (AppMemory *)pPlatformCtx->pPermanentMemory;
	pMem->Log.Init();
	pMem->Log.LoadFilter(pPlatformCtx);
	
	
	
}

void AppShutdown(PlatformReloadContext* pPlatformReloadCtx)
{
	
}

void AppOnHotReload(bool Started, PlatformReloadContext* pPlatformReloadCtx)
{
	if (Started) 
	{
		
	} 
	else 
	{
		ImGui::SetCurrentContext(pPlatformReloadCtx->pImGuiCtx);
		ImGui::SetAllocatorFunctions(pPlatformReloadCtx->pImGuiAllocFunc, pPlatformReloadCtx->pImGuiFreeFunc);
	}
}

void AppOnDrop(PlatformContext* pPlatformCtx, char* FileName) 
{
	AppMemory *pMem = (AppMemory *)pPlatformCtx->pPermanentMemory;
	memcpy(pMem->Log.aFilePathToLoad, FileName, ArrayCount(pMem->Log.aFilePathToLoad));
	pMem->Log.LoadFile(pPlatformCtx);
}