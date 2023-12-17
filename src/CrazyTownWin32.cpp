#include <windows.h>
#include <d3d11.h>
#include <oleidl.h>

//==================================================

#include "HotReloadUtils.h"
#include "StringUtils.h"
#include "SharedDefinitions.h"
#include "SomeMacros.h"
#include <comdef.h>

//==================================================
// ImGUI Section

#include "../vendor/imgui.cpp"
#include "../vendor/imgui_draw.cpp"
#include "../vendor/imgui_impl_dx11.cpp"
#include "../vendor/imgui_impl_win32.cpp"
#include "../vendor/imgui_tables.cpp"
#include "../vendor/imgui_widgets.cpp"

//==================================================
// Global Section

static PlatformReloadContext gPlatformReloadContext;
static PlatformContext gPlatformContext;
static int64_t gPerfCountFrequency;
static HotReloadableDll gHotReloadableCode;

ID3D11Device*            g_pd3dDevice = nullptr;
ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
IDXGISwapChain*          g_pSwapChain = nullptr;
UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;


#define DRAG_AND_DROP 1

#if DRAG_AND_DROP
class DropTarget : public IDropTarget {
	
public:
	DropTarget(){};
	
	DropTarget(HWND hwnd) : m_refCount(1), m_hWnd(hwnd) {
	}

	STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
		// QueryInterface allows the caller to request interfaces supported by this object.
		if (riid == IID_IDropTarget) {
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		
		// If the requested interface is not supported, set ppvObject to nullptr and return E_NOINTERFACE.
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}
	
	STDMETHODIMP_(ULONG) AddRef() {
		return InterlockedIncrement(&m_refCount);
	}

	STDMETHODIMP_(ULONG) Release() {
		ULONG refCount = InterlockedDecrement(&m_refCount);
		if (refCount == 0) {
			delete this;
		}
		return refCount;
	}

	// IDropTarget methods
	STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
		// Check if the data object contains a file drop format (CF_HDROP)
		FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
		if (pDataObj->QueryGetData(&fmt) == S_OK) {
			// The data object contains a file drop, so we allow a "Copy" operation.
			// Set pdwEffect to DROPEFFECT_COPY to indicate this.
			*pdwEffect = DROPEFFECT_COPY;
			return S_OK;
		}

		// The data object does not contain a file drop format, so we disallow the drop operation.
		// Set pdwEffect to DROPEFFECT_NONE to indicate no allowed drop operation.
		*pdwEffect = DROPEFFECT_NONE;
		return S_OK;
	}

	STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
		// Called when the mouse is moved over the window during a drag operation.

		// Set the drop effect to "Copy" while dragging over the window.
		*pdwEffect = DROPEFFECT_COPY;
		return S_OK;
	}

	STDMETHODIMP DragLeave() {
		// Called when a drag operation leaves the window area.
		return S_OK;
	}

	STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
		// Called when a drag operation is released (the file is dropped) over the window.

		// Get the dropped file path from the data object.
		FORMATETC fmt = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
		STGMEDIUM stg;
		if (pDataObj->GetData(&fmt, &stg) == S_OK) {
			HDROP hDrop = static_cast<HDROP>(stg.hGlobal);
			UINT fileCount = DragQueryFile(hDrop, 0xFFFFFFFF, nullptr, 0);

			if (fileCount > 0) {
				TCHAR filePath[MAX_PATH];
				DragQueryFile(hDrop, 0, filePath, MAX_PATH);
				gHotReloadableCode.pOnDropFunc(&gPlatformContext, filePath);
			}
			
			// Release the storage medium used for the file path data.
			ReleaseStgMedium(&stg);
		}

		// Indicate that the drop operation has been completed successfully.
		// Set pdwEffect to DROPEFFECT_COPY to indicate a "Copy" operation.
		*pdwEffect = DROPEFFECT_COPY;
		return S_OK;
	}

	LONG m_refCount;
	HWND m_hWnd;
};
#endif

void ImGUIMemFree(void* ptr, void* user_data) {
	IM_UNUSED(user_data); 
	free(ptr);
}

void* ImGUIMemAlloc(size_t sz, void* user_data) {
	IM_UNUSED(user_data); 
	return malloc(sz);
}

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CleanupRenderTarget();
void CreateRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//==================================================
// IO Section

bool Win32WriteFile(FileContent* pFileContent, char* pPath) 
{
	bool Result = false;

	HANDLE FileHandle =	CreateFileA(pPath, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
	if(FileHandle != INVALID_HANDLE_VALUE)	
	{
		DWORD BytesWritten;
		if(WriteFile(FileHandle, pFileContent->pFile, (DWORD)pFileContent->Size, &BytesWritten, 0))
		{
			// File write success
			Result = (BytesWritten == pFileContent->Size);
		}

		CloseHandle(FileHandle);
	}

	return Result;
}

void Win32FreeFile(FileContent* pFileContent)
{
	if (pFileContent->pFile)
	{
		VirtualFree(pFileContent->pFile, 0, MEM_RELEASE);
		pFileContent->pFile = 0;
	}
}


FileContent Win32ReadFile(char* pPath) 
{
	FileContent Result = {0};
	HANDLE FileHandle =	CreateFileA(pPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
	if(FileHandle != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER FileSize;
		if(GetFileSizeEx(FileHandle, &FileSize))
		{
			uint32_t FileSize32 = (uint32_t)(FileSize.QuadPart); 
			Result.pFile = VirtualAlloc(0, FileSize32, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
			if(Result.pFile)
			{
				DWORD BytesRead;
				if(ReadFile(FileHandle, Result.pFile, FileSize32, &BytesRead, 0) && (FileSize32 == BytesRead))
				{
					// File read success
					Result.Size = FileSize32;
				}
				else
				{
					Win32FreeFile(&Result);
					Result.pFile = 0;
				}
			}
		}
			
		CloseHandle(FileHandle);
	}
	else
	{
		LPVOID pErrorMsgBuffer = NULL;
		DWORD Error = GetLastError();
		DWORD ErrorResult = FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			NULL,
			Error,
			0, // Default language
			(LPSTR)&pErrorMsgBuffer,
			0,
			NULL
		);
		
		OutputDebugStringA((LPSTR)pErrorMsgBuffer);
	}
	
	return Result;
}

bool Win32FetchLastFileFolder(char* pFolderQuery, FileData* pLastFileTimeData, FileData* pOutLastFileFolder)
{
	// Check the proper format
	// We expect to receive FolderPath\*.extension
	char pExtensionToken = '.';
	char pExtensionQueryToken = '*';
	size_t FolderQueryLen = StringUtils::Length(pFolderQuery);
	size_t FolderPathLen = 0;
	bool bQueryWithExpectedFormat = false;
	if (FolderQueryLen)
	{
		for (size_t i = FolderQueryLen; i != 0; i--) 
		{
			if (pFolderQuery[i] == pExtensionToken && pFolderQuery[i-1] == pExtensionQueryToken) {
				
				bQueryWithExpectedFormat = true;
				FolderPathLen = i - 1; 
				
				memcpy(pOutLastFileFolder->aFilePath, pFolderQuery, sizeof(char) * FolderPathLen);
				
				break;
			}
		}
	}
	
	if (!bQueryWithExpectedFormat)
		return false;
	
	bool bFoundNewFile = false;
		
	FILETIME BestDate = *(FILETIME*)pLastFileTimeData->FileTime.aWriteTime;
	
	WIN32_FIND_DATA FindData;
	HANDLE hFind = FindFirstFile(pFolderQuery, &FindData);

	if (hFind != INVALID_HANDLE_VALUE) 
	{
		do 
		{
			bool bIsNewer = CompareFileTime(&FindData.ftLastWriteTime, &BestDate) >= 0;
			int CreationTimeCompare = CompareFileTime(&FindData.ftCreationTime, (FILETIME*)pLastFileTimeData->FileTime.aCreationTime) != 0;
			
			if (bIsNewer)
			{
				if (CreationTimeCompare >= 0 && (FindData.nFileSizeLow > 0))
				{
					size_t RemainingSize = sizeof(pOutLastFileFolder->aFilePath) - sizeof(char) * (FolderPathLen);
					strcpy_s(&pOutLastFileFolder->aFilePath[FolderPathLen],
					         RemainingSize,
					         FindData.cFileName);
					
					bool bIsDifferent = false;
					// If both files have the same creation time make sure that have the same name
					if (CreationTimeCompare == 0)
					{
						bIsDifferent = strcmp(pOutLastFileFolder->aFilePath, pLastFileTimeData->aFilePath) != 0;
					}
					else
					{
						bIsDifferent = true;
					}
					
					if (bIsDifferent)
					{
						bFoundNewFile = true;
				
						BestDate = FindData.ftLastWriteTime;
				
						memcpy(&pOutLastFileFolder->FileTime.aWriteTime, &FindData.ftLastWriteTime, sizeof(FILETIME));
						memcpy(&pOutLastFileFolder->FileTime.aCreationTime, &FindData.ftCreationTime, sizeof(FILETIME));
					}
				}
			}
				
		} while (FindNextFile(hFind, &FindData));

		FindClose(hFind);
	}

	return bFoundNewFile;
}

void Win32OpenURL(const char* pURL)
{
	ShellExecuteA(NULL, "open", pURL, NULL, NULL, SW_SHOWNORMAL);
}

//==================================================
// Time Section

inline LARGE_INTEGER Win32GetWallClock() 
{
	LARGE_INTEGER Result;
	QueryPerformanceCounter(&Result);
	return Result;
}

inline float Win32GetSecondsElapsed(LARGE_INTEGER Start, LARGE_INTEGER End)
{
	return (float)(End.QuadPart - Start.QuadPart) / (float)gPerfCountFrequency;
}

//==================================================

int WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CommandLine, int ShowCode)
{
	char aEXEFullPath[MAX_PATH];
	char* pPastLastSlash = nullptr;
	char aHotReloadDLLFullPath[MAX_PATH];
	char aHotReloadTempDLLFullPath[MAX_PATH];
	char aBuildingMarkerPath[MAX_PATH];
	
	// Fill Exe and HotReloadable Full path
	{
		GetEXEFileName(aEXEFullPath, sizeof(aEXEFullPath), &pPastLastSlash);
	
		size_t SizeUpToPastLastSlash = pPastLastSlash - aEXEFullPath;
	
		const char* p_HotReloadDLLName = "CrazyTownApp.dll";
		size_t SizeOfRelodableDllName = StringUtils::Length(p_HotReloadDLLName);
	
		StringUtils::Concat(aHotReloadDLLFullPath, sizeof(aHotReloadDLLFullPath),
		                    aEXEFullPath, SizeUpToPastLastSlash,
		                    p_HotReloadDLLName, SizeOfRelodableDllName);
	                    
		const char* p_HotReloadTempDLLName = "CrazyTownAppTemp.dll";	
		size_t SizeOfRelodableTempDllName = strlen(p_HotReloadTempDLLName);
	
		StringUtils::Concat(aHotReloadTempDLLFullPath, sizeof(aHotReloadTempDLLFullPath),
		                    aEXEFullPath, SizeUpToPastLastSlash,
		                    p_HotReloadTempDLLName, SizeOfRelodableTempDllName);
		
		const char* p_BuildingMarkerName = "building_marker";	
		size_t SizeOfBuildingMarkerName = strlen(p_BuildingMarkerName);
	
		StringUtils::Concat(aBuildingMarkerPath, sizeof(aBuildingMarkerPath),
		                    aEXEFullPath, SizeUpToPastLastSlash,
		                    p_BuildingMarkerName, SizeOfBuildingMarkerName);
	}
	
	//==================================================
	// Initialize ImGUI	
	
	// Create application window
	//ImGui_ImplWin32_EnableDpiAwareness();
	WNDCLASSEXW wc = { 
		sizeof(wc), 
		CS_CLASSDC, 
		WndProc, 
		0L, 
		0L, 
		GetModuleHandle(nullptr), 
		nullptr, nullptr, nullptr, nullptr, 
		L"CrazyTown", 
		nullptr };
	
	
	
	::RegisterClassExW(&wc);
	HWND hwnd = ::CreateWindowExW(
#if DEVELOPMENT 
		WS_EX_TOPMOST,
#else
		0,
#endif
		wc.lpszClassName, 
		L"CrazyTown", 
		WS_OVERLAPPEDWINDOW, 
		0, 620, 
		1200, 800, 
		nullptr, nullptr, 
		wc.hInstance, 
		nullptr);
	

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd))
	{
		CleanupDeviceD3D();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// Show the window
	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);
	
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
	// NOTE(Matiasp): allowing font scaling it will break peek.
	//io.FontAllowUserScaling = true; 
	
	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
	
	gPlatformReloadContext.pImGuiCtx = ImGui::GetCurrentContext();
	gPlatformReloadContext.pImGuiAllocFunc = ImGUIMemAlloc;
	gPlatformReloadContext.pImGuiFreeFunc = ImGUIMemFree;
	
	gPlatformContext.PermanentMemoryCapacity = Megabytes(100);
	gPlatformContext.ScratchMem.Capacity= Megabytes(100);
	
	uint64_t MemorySize = gPlatformContext.PermanentMemoryCapacity + gPlatformContext.ScratchMem.Capacity;
	void* p_AllocatedMemory = VirtualAlloc(0, (size_t)MemorySize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	gPlatformContext.pPermanentMemory = p_AllocatedMemory;
	gPlatformContext.ScratchMem.pMemory = (uint8_t*)p_AllocatedMemory + gPlatformContext.PermanentMemoryCapacity;
	
	gPlatformContext.pReadFileFunc = Win32ReadFile;
	gPlatformContext.pWriteFileFunc = Win32WriteFile;
	gPlatformContext.pFreeFileContentFunc = Win32FreeFile;
	gPlatformContext.pFetchLastFileFolderFunc = Win32FetchLastFileFolder;
	gPlatformContext.pOpenURLFunc = Win32OpenURL;
	
	gHotReloadableCode = HotReloadDll(aHotReloadDLLFullPath, aHotReloadTempDLLFullPath);
	gHotReloadableCode.pInitFunc(&gPlatformContext, &gPlatformReloadContext);
	
	// NOTE(MatiasP): Set the windows scheduler granularity to 1ms,
	// so that our Sleep() can be more granular.
	UINT DesiredSchedulerMS = 1;
	bool SleepIsGranular = timeBeginPeriod(DesiredSchedulerMS) == TIMERR_NOERROR;
	
	LARGE_INTEGER PerfCountFrequencyResult;
	QueryPerformanceFrequency(&PerfCountFrequencyResult);
	gPerfCountFrequency = PerfCountFrequencyResult.QuadPart;
	
	LARGE_INTEGER LastUpdateCounter = Win32GetWallClock();
	LARGE_INTEGER LastCounter = Win32GetWallClock();
	float TargetSecondsPerFrame = 1.0f / (float)45.f;
	
	DropTarget DragDrop;
	
#if DRAG_AND_DROP
	HRESULT hr = OleInitialize(nullptr);
	if (!FAILED(hr)) 
	{
		DragDrop = DropTarget(hwnd);
		hr =::RegisterDragDrop(hwnd,static_cast<IDropTarget*>(&DragDrop));
		if (FAILED(hr))
		{
			OleUninitialize();
			DragDrop.m_hWnd = 0;
		}
	}
#endif
	
	

	//==================================================
	// Main loop
	
	bool IsRunning = true;
	while (IsRunning)
	{	
		gPlatformContext.ScratchMem.Size = 0;
		
		FILETIME NewDLLWriteTime = GetLastWriteTime(aHotReloadDLLFullPath);
		
		// MSDOC: 1 means First file time is later than second file time.
		if (CompareFileTime(&NewDLLWriteTime, &gHotReloadableCode.LastWriteTime) == 1) 
		{
			DWORD attrib = GetFileAttributes(aBuildingMarkerPath);
			bool BuildingMarkerExist = (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
			if (!BuildingMarkerExist) 
			{
				OutputDebugStringA("Reloading! \n");
				gHotReloadableCode.pOnHotReloadFunc(true, &gPlatformReloadContext);
				UnloadHotReloadDLL(&gHotReloadableCode);
				gHotReloadableCode = HotReloadDll(aHotReloadDLLFullPath, aHotReloadTempDLLFullPath);
				gHotReloadableCode.pOnHotReloadFunc(false, &gPlatformReloadContext);
			}
		}
		
		//==================================================
		// ImGUI Windows events handle

		// Poll and handle messages (inputs, window resize, etc.)
		// See the WndProc() function below for our to dispatch events to the Win32 backend.
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				IsRunning = false;
		}
		
		if (!IsRunning)
			break;

		// Handle window resize (we don't resize directly in the WM_SIZE handler)
		if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
		{
			CleanupRenderTarget();
			g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
			g_ResizeWidth = g_ResizeHeight = 0;
			CreateRenderTarget();
		}
		
		gHotReloadableCode.pPreUpdateFunc(&gPlatformContext);
		if (gPlatformContext.bWantsToRebuildFontTexture)
		{
			ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
			if (bd->pd3dDevice)
			{
				if (bd->pFontSampler)           { bd->pFontSampler->Release(); bd->pFontSampler = nullptr; }
				if (bd->pFontTextureView)       { bd->pFontTextureView->Release(); bd->pFontTextureView = nullptr; ImGui::GetIO().Fonts->SetTexID(0); } // We copied data->pFontTextureView to io.Fonts->TexID so let's clear that as well.
				ImGui_ImplDX11_CreateFontsTexture();
			}
			
			gPlatformContext.bWantsToRebuildFontTexture = false;
		}
		
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		
		LARGE_INTEGER CounterBeforeUpdate = Win32GetWallClock();
		float DeltaTime = Win32GetSecondsElapsed(LastUpdateCounter, CounterBeforeUpdate);
		
		gHotReloadableCode.pUpdateFunc(DeltaTime, &gPlatformContext);
		
		LastUpdateCounter = CounterBeforeUpdate;
		
		//==================================================
		// ImGUI Render
	
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		
		// Rendering
		ImGui::Render();
		const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		// Update and Render additional Platform Windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		g_pSwapChain->Present(1, 0); // Present with vsync
		
		LARGE_INTEGER CounterAfterUpdate = Win32GetWallClock();
		float WorkSecondsElapsed = Win32GetSecondsElapsed(LastCounter, CounterAfterUpdate);

		float SecondsElapsedForFrame = WorkSecondsElapsed;
		if (SecondsElapsedForFrame < TargetSecondsPerFrame) {
			if (SleepIsGranular)
			{
				DWORD SleepMS = (DWORD)(1000.0f * (TargetSecondsPerFrame - SecondsElapsedForFrame));
				if (SleepMS > 0)
				{
					Sleep(SleepMS);
				}
			}
		}
		
		
		
		LARGE_INTEGER EndCounter = Win32GetWallClock();
		LastCounter = EndCounter;
	};
	
	gHotReloadableCode.pShutdownFunc(&gPlatformReloadContext);
	
	
#if DRAG_AND_DROP
	if (DragDrop.m_hWnd != 0) {
		RevokeDragDrop(hwnd);
		OleUninitialize();
	}
#endif
	
	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);
	
	return 0;
}

//==================================================
// ImGUI Helper functions

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;
	
	switch (msg)
	{
		case WM_SIZE:
			if (wParam == SIZE_MINIMIZED)
				return 0;
			g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
			g_ResizeHeight = (UINT)HIWORD(lParam);
			return 0;
		case WM_SYSCOMMAND:
			if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
				return 0;
			break;
		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
		case WM_DPICHANGED:
			{
				const RECT* suggested_rect = (RECT*)lParam;
				::SetWindowPos(hWnd, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
			}
			break;
	}
	
	
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	//createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
	if (res != S_OK)
		return false;

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { 
		g_pSwapChain->Release(); 
		g_pSwapChain = nullptr; 
	}
	
	if (g_pd3dDeviceContext) {
		g_pd3dDeviceContext->Release(); 
		g_pd3dDeviceContext = nullptr;
	}
	
	if (g_pd3dDevice) { 
		g_pd3dDevice->Release(); 
		g_pd3dDevice = nullptr;
	}
}

void CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
	pBackBuffer->Release();
}

void CleanupRenderTarget()
{
	if (g_mainRenderTargetView) {
		g_mainRenderTargetView->Release(); 
		g_mainRenderTargetView = nullptr;
	}
}


