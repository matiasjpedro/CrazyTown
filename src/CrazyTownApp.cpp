#include "SomeMacros.h"

#include "../vendor/imgui.cpp"
#include "../vendor/imgui_demo.cpp"
#include "../vendor/imgui_draw.cpp"
#include "../vendor/imgui_tables.cpp"
#include "../vendor/imgui_widgets.cpp"

#include "../vendor/cJSON.c"

#include "SharedDefinitions.cpp"
#include "CrazyTextFilter.cpp"
#include "CrazyLog.cpp"

struct AppMemory 
{
	CrazyLog Log;
};

void AppPreUpdate(PlatformContext* pPlatformCtx)
{
	AppMemory *pMem = (AppMemory *)pPlatformCtx->pPermanentMemory;
	pMem->Log.PreDraw(pPlatformCtx);
}

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

void AppInit(PlatformContext* pPlatformCtx, PlatformReloadContext* pPlatformReloadCtx) 
{
	ImGui::SetCurrentContext(pPlatformReloadCtx->pImGuiCtx);
	ImGui::SetAllocatorFunctions(pPlatformReloadCtx->pImGuiAllocFunc, pPlatformReloadCtx->pImGuiFreeFunc);
	
	ASSERT(sizeof(AppMemory) <= pPlatformCtx->PermanentMemoryCapacity);
	AppMemory *pMem = (AppMemory *)pPlatformCtx->pPermanentMemory;
	pMem->Log.Init();
	pMem->Log.LoadFilter(pPlatformCtx);
	pMem->Log.LoadFolderQuery(pPlatformCtx);
	
	pMem->Log.BuildFonts();
	
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
	pMem->Log.bStreamMode = false;
	pMem->Log.bFolderQuery = false;
}
