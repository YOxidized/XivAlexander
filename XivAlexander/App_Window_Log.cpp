#include "pch.h"
#include "App_Window_Log.h"
#include "resource.h"

constexpr int BaseFontSize = 9;

static const std::map<App::LogLevel, int> LogLevelStyleMap{
	{App::LogLevel::Debug, STYLE_LASTPREDEFINED + 0},
	{App::LogLevel::Info, STYLE_LASTPREDEFINED + 1},
	{App::LogLevel::Warning, STYLE_LASTPREDEFINED + 2},
	{App::LogLevel::Error, STYLE_LASTPREDEFINED + 3},
};

static const std::map<App::LogCategory, const char*> LogCategoryNames{
	{App::LogCategory::General, "General"},
	{App::LogCategory::SocketHook, "SocketHook"},
	{App::LogCategory::AllIpcMessageLogger, "AllIpcMessageLogger"},
	{App::LogCategory::AnimationLockLatencyHandler, "AnimationLockLatencyHandler"},
	{App::LogCategory::EffectApplicationDelayLogger, "EffectApplicationDelayLogger"},
	{App::LogCategory::IpcTypeFinder, "IpcTypeFinder"},
};

static WNDCLASSEXW WindowClass() {
	const auto hIcon = Utils::Win32::Closeable::Icon(LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_TRAY_ICON)),
		nullptr,
		"Failed to load app icon.");
	WNDCLASSEXW wcex;
	ZeroMemory(&wcex, sizeof wcex);
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = g_hInstance;
	wcex.hIcon = hIcon;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_LOG_MENU);
	wcex.lpszClassName = L"XivAlexander::Window::Log";
	wcex.hIconSm = hIcon;
	return wcex;
}

App::Window::Log::Log()
	: Base(WindowClass(), L"Log", WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr) {

	NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0);

	m_hScintilla = CreateWindowExW(0, TEXT("Scintilla"), TEXT(""), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
		0, 0, 0, 0, m_hWnd, nullptr, g_hInstance, nullptr);
	m_direct = reinterpret_cast<SciFnDirect>(SendMessageW(m_hScintilla, SCI_GETDIRECTFUNCTION, 0, 0));
	m_directPtr = SendMessageW(m_hScintilla, SCI_GETDIRECTPOINTER, 0, 0);
	m_direct(m_directPtr, SCI_STYLESETSIZE, STYLE_DEFAULT, static_cast<int>(BaseFontSize * GetZoom()));
	m_direct(m_directPtr, SCI_SETWRAPMODE, SC_WRAP_CHAR, 0);
	m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
	m_direct(m_directPtr, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 1, 0);
	m_direct(m_directPtr, SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(Utils::ToUtf8(ncm.lfMessageFont.lfFaceName).c_str()));
	m_direct(m_directPtr, SCI_STYLESETFORE, LogLevelStyleMap.at(LogLevel::Debug), RGB(80, 80, 80));
	m_direct(m_directPtr, SCI_STYLESETFORE, LogLevelStyleMap.at(LogLevel::Info), RGB(0, 0, 0));
	m_direct(m_directPtr, SCI_STYLESETFORE, LogLevelStyleMap.at(LogLevel::Warning), RGB(160, 160, 0));
	m_direct(m_directPtr, SCI_STYLESETFORE, LogLevelStyleMap.at(LogLevel::Error), RGB(255, 80, 80));

	const auto addLogFn = [&](const Misc::Logger::LogItem& item) {
		const auto st = item.TimestampAsLocalSystemTime();
		const auto logstr = Utils::FormatString("%04d-%02d-%02d %02d:%02d:%02d.%03d\t%s\t%s\n",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond,
			st.wMilliseconds,
			LogCategoryNames.at(item.category),
			item.log.c_str());
		RunOnUiThreadWait([&]() {
			SendMessage(m_hScintilla, WM_SETREDRAW, FALSE, 0);
			m_direct(m_directPtr, SCI_SETREADONLY, FALSE, 0);
			auto nPos = m_direct(m_directPtr, SCI_GETLENGTH, 0, 0);
			auto nLineCount = m_direct(m_directPtr, SCI_GETLINECOUNT, 0, 0) - 1;
			const auto nFirstLine = m_direct(m_directPtr, SCI_GETFIRSTVISIBLELINE, 0, 0);
			const auto nLinesOnScreen = m_direct(m_directPtr, SCI_LINESONSCREEN, 0, 0);
			const auto atBottom = nFirstLine >= nLineCount - nLinesOnScreen && m_direct(m_directPtr, SCI_GETSELECTIONEMPTY, 0, 0);
			m_direct(m_directPtr, SCI_STARTSTYLING, nPos, 0);

			m_direct(m_directPtr, SCI_APPENDTEXT, logstr.length(), reinterpret_cast<sptr_t>(logstr.c_str()));
			m_direct(m_directPtr, SCI_SETSTYLING, logstr.length(), LogLevelStyleMap.at(item.level));
			nPos += logstr.length();
			nLineCount++;
			if (nLineCount > 32768) {
				const auto deleteTo = m_direct(m_directPtr, SCI_POSITIONFROMLINE, nLineCount - 32768, 0);
				m_direct(m_directPtr, SCI_DELETERANGE, 0, deleteTo);
			}
			if (atBottom) {
				m_direct(m_directPtr, SCI_SETFIRSTVISIBLELINE, INT_MAX, 0);
			}
			m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
			SendMessage(m_hScintilla, WM_SETREDRAW, TRUE, 0);
			InvalidateRect(m_hScintilla, nullptr, FALSE);
			return 0;
			});
	};

	const auto& logger = Misc::Logger::GetLogger();
	for (const auto item : logger.GetLogs())
		addLogFn(*item);
	m_callbackHandle = Misc::Logger::GetLogger().OnNewLogItem(addLogFn);

	ShowWindow(m_hWnd, SW_SHOW);
}

App::Window::Log::~Log() {
	Destroy();
}

void App::Window::Log::OnLayout(double zoom, double width, double height) {
	SetWindowPos(m_hScintilla, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height), 0);
	ResizeMargin();
}

LRESULT App::Window::Log::WndProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_COMMAND: {
			switch (LOWORD(wParam)) {
				case ID_FILE_SAVE: {
					struct DataT {
						HWND hWnd;
						std::string buf;
					};

					auto pDataT = new DataT();
					pDataT->hWnd = m_hWnd;
					pDataT->buf = std::string(m_direct(m_directPtr, SCI_GETLENGTH, 0, 0) + 1, '\0');
					m_direct(m_directPtr, SCI_GETTEXT, pDataT->buf.length(), reinterpret_cast<sptr_t>(&pDataT->buf[0]));
					pDataT->buf.resize(pDataT->buf.length() - 1);

					Utils::Win32::Closeable::Handle hThread(CreateThread(nullptr, 0, [](void* pDataRaw) -> DWORD {
						auto pDataT = static_cast<DataT*>(pDataRaw);
						Utils::CallOnDestruction freeDataT([pDataT]() { delete pDataT; });
						static const COMDLG_FILTERSPEC saveFileTypes[] = {
							{L"Log Files (*.log)",		L"*.log"},
							{L"All Documents (*.*)",	L"*.*"}
						};
						auto throw_on_error = [](HRESULT val) {
							if (!SUCCEEDED(val))
								_com_raise_error(val);
						};

						Utils::Win32::SetThreadDescription(GetCurrentThread(), L"XivAlexander::Window::Log::WndProc::FileSaveThread(%p)", pDataT->hWnd);

						try {
							IFileSaveDialogPtr pDialog;
							DWORD dwFlags;
							throw_on_error(pDialog.CreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER));
							throw_on_error(pDialog->SetFileTypes(ARRAYSIZE(saveFileTypes), saveFileTypes));
							throw_on_error(pDialog->SetFileTypeIndex(0));
							throw_on_error(pDialog->SetDefaultExtension(L"log"));
							throw_on_error(pDialog->GetOptions(&dwFlags));
							throw_on_error(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
							if (SUCCEEDED(pDialog->Show(pDataT->hWnd))) {
								IShellItemPtr pResult;
								PWSTR pszNewFileName;
								throw_on_error(pDialog->GetResult(&pResult));
								throw_on_error(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszNewFileName));
								if (!pszNewFileName)
									throw std::runtime_error("The selected file does not have a filesystem path.");

								Utils::CallOnDestruction freeFileName([pszNewFileName]() { CoTaskMemFree(pszNewFileName); });
								{
									const Utils::Win32::Closeable::Handle hFile(CreateFile(pszNewFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr),
										INVALID_HANDLE_VALUE,
										L"Failed to open file: %s", pszNewFileName);
									DWORD written;
									WriteFile(hFile, pDataT->buf.data(), static_cast<DWORD>(pDataT->buf.length()), &written, nullptr);
									if (written != pDataT->buf.length())
										throw std::runtime_error("Failed to fully write the log file.");
								}

								Utils::Win32::MessageBoxF(pDataT->hWnd, MB_ICONINFORMATION, L"XivAlexander", L"Log saved to: %s", pszNewFileName);
							}
						} catch (std::exception& e) {
							MessageBoxW(pDataT->hWnd, Utils::FromUtf8(e.what()).c_str(), L"XivAlexander", MB_ICONERROR);
						} catch (_com_error& e) {
							MessageBoxW(pDataT->hWnd, e.Description(), L"XivAlexander", MB_ICONERROR);
						}
						return 0;
						}, pDataT, 0, nullptr),
						Utils::Win32::Closeable::Handle::Null,
							"Failed to start FileSaveThread.");
					return 0;
				}

				case ID_FILE_CLEAR: {
					Misc::Logger::GetLogger().Clear();
					m_direct(m_directPtr, SCI_SETREADONLY, FALSE, 0);
					m_direct(m_directPtr, SCI_CLEARALL, 0, 0);
					m_direct(m_directPtr, SCI_SETREADONLY, TRUE, 0);
					return 0;
				}

				case ID_VIEW_ALWAYSONTOP: {
					const auto hMenu = GetMenu(m_hWnd);
					MENUITEMINFOW menuInfo = { sizeof(MENUITEMINFOW) };
					menuInfo.fMask = MIIM_STATE;
					GetMenuItemInfo(hMenu, ID_VIEW_ALWAYSONTOP, FALSE, &menuInfo);
					if (GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) {
						SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
						menuInfo.fState &= ~MFS_CHECKED;
					} else {
						SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
						menuInfo.fState |= MFS_CHECKED;
					}
					SetMenuItemInfoW(hMenu, ID_VIEW_ALWAYSONTOP, FALSE, &menuInfo);
				}
			}
			break;
		}
	}
	return Base::WndProc(uMsg, wParam, lParam);
}

LRESULT App::Window::Log::OnNotify(const LPNMHDR nmhdr) {
	if (nmhdr->hwndFrom == m_hScintilla) {
		const auto nm = reinterpret_cast<SCNotification*>(nmhdr);
		if (nmhdr->code == SCN_ZOOM) {
			ResizeMargin();
		}
	}
	return Base::OnNotify(nmhdr);
}

void App::Window::Log::OnDestroy() {
	Config::Instance().Runtime.ShowLoggingWindow = false;
}

void App::Window::Log::ResizeMargin() {
	m_direct(m_directPtr, SCI_SETMARGINWIDTHN, 0, static_cast<int>(m_direct(m_directPtr, SCI_TEXTWIDTH, static_cast<uptr_t>(STYLE_LINENUMBER), reinterpret_cast<sptr_t>("999999"))));
}
