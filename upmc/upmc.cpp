#ifndef UNICODE
#define UNICODE
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <Windows.h>
#include "hidapi.h"
#include "upmc.h"
#include "terminal.h"
#include <tchar.h>
#include <strsafe.h>
#include <dbt.h>
#include <fstream>
#include <iostream>

using namespace std::chrono;
using namespace usbcl;
using namespace terminal;
using namespace std;

volatile bool isRunning = true;

SYSTEMTIME st;

GUID WceusbshGUID = { 0x4d36e978,0xe325,0x11ce,0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18 };

// For informational messages and window titles.
PWSTR g_pszAppName;

void OutputMessage(
	HWND hOutWnd,
	WPARAM wParam,
	LPARAM lParam
)
// Routine Description:
//     Support routine.
//     Send text to the output window, scrolling if necessary.

// Parameters:
//     hOutWnd - Handle to the output window.
//     wParam  - Standard windows message code, not used.
//     lParam  - String message to send to the window.

// Return Value:
//     None

// Note:
//     This routine assumes the output window is an edit control
//     with vertical scrolling enabled.

//     This routine has no error-checking.
{
	LRESULT   lResult;
	LONG      bufferLen;
	LONG      numLines;
	LONG      firstVis;

	// Make writable and turn off redraw.
	lResult = SendMessage(hOutWnd, EM_SETREADONLY, FALSE, 0L);
	lResult = SendMessage(hOutWnd, WM_SETREDRAW, FALSE, 0L);

	// Obtain current text length in the window.
	bufferLen = SendMessage(hOutWnd, WM_GETTEXTLENGTH, 0, 0L);
	numLines = SendMessage(hOutWnd, EM_GETLINECOUNT, 0, 0L);
	firstVis = SendMessage(hOutWnd, EM_GETFIRSTVISIBLELINE, 0, 0L);
	lResult = SendMessage(hOutWnd, EM_SETSEL, bufferLen, bufferLen);

	// Write the new text.
	lResult = SendMessage(hOutWnd, EM_REPLACESEL, 0, lParam);

	// See whether scrolling is necessary.
	if (numLines > (firstVis + 1))
	{
		int        lineLen = 0;
		int        lineCount = 0;
		int        charPos;

		// Find the last nonblank line.
		numLines--;
		while (!lineLen)
		{
			charPos = SendMessage(
				hOutWnd, EM_LINEINDEX, (WPARAM)numLines, 0L);
			lineLen = SendMessage(
				hOutWnd, EM_LINELENGTH, charPos, 0L);
			if (!lineLen)
				numLines--;
		}
		// Prevent negative value finding min.
		lineCount = numLines - firstVis;
		lineCount = (lineCount >= 0) ? lineCount : 0;

		// Scroll the window.
		lResult = SendMessage(
			hOutWnd, EM_LINESCROLL, 0, (LPARAM)lineCount);
	}

	// Done, make read-only and allow redraw.
	lResult = SendMessage(hOutWnd, WM_SETREDRAW, TRUE, 0L);
	lResult = SendMessage(hOutWnd, EM_SETREADONLY, TRUE, 0L);
}

void ErrorHandler(
	LPCTSTR lpszFunction
)
// Routine Description:
//     Support routine.
//     Retrieve the system error message for the last-error code
//     and pop a modal alert box with usable info.

// Parameters:
//     lpszFunction - String containing the function name where 
//     the error occurred plus any other relevant data you'd 
//     like to appear in the output. 

// Return Value:
//     None

// Note:
//     This routine is independent of the other windowing routines
//     in this application and can be used in a regular console
//     application without modification.
{

	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0, NULL);

	// Display the error message and exit the process.

	lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
		(lstrlen((LPCTSTR)lpMsgBuf)
			+ lstrlen((LPCTSTR)lpszFunction) + 40)
		* sizeof(TCHAR));
	if (!lpDisplayBuf) return;
	StringCchPrintf((LPTSTR)lpDisplayBuf,
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("%s failed with error %d: %s"),
		lpszFunction, dw, (LPCTSTR)lpMsgBuf);
	MessageBox(NULL, (LPCTSTR)lpDisplayBuf, g_pszAppName, MB_OK);

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
}

BOOL DoRegisterDeviceInterfaceToHwnd(
	IN GUID InterfaceClassGuid,
	IN HWND hWnd,
	OUT HDEVNOTIFY* hDeviceNotify
)
// Routine Description:
//     Registers an HWND for notification of changes in the device interfaces
//     for the specified interface class GUID. 

// Parameters:
//     InterfaceClassGuid - The interface class GUID for the device 
//         interfaces. 

//     hWnd - Window handle to receive notifications.

//     hDeviceNotify - Receives the device notification handle. On failure, 
//         this value is NULL.

// Return Value:
//     If the function succeeds, the return value is TRUE.
//     If the function fails, the return value is FALSE.

// Note:
//     RegisterDeviceNotification also allows a service handle be used,
//     so a similar wrapper function to this one supporting that scenario
//     could be made from this template.
{
	DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;

	ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
	NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
	NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	NotificationFilter.dbcc_classguid = InterfaceClassGuid;

	*hDeviceNotify = RegisterDeviceNotification(
		hWnd,                       // events recipient
		&NotificationFilter,        // type of device
		DEVICE_NOTIFY_WINDOW_HANDLE // type of recipient handle
	);

	if (NULL == *hDeviceNotify)
	{
		ErrorHandler(L"RegisterDeviceNotification");
		return FALSE;
	}

	return TRUE;
}

class UsbPowerMeter {
public:
	hid_device* handle = NULL;
	bool available = false;
	bool paused = false;
	float current;
	float voltage;
	float power;
	int totalTime;
	unsigned long milis;
	bool record;
	ofstream saveFile;

	bool availableTest()
	{
		if (this->handle == NULL) {
			return false;
		}
		return true;
	}

	bool begin(int vid, int pid)
	{
		this->handle = hid_open(vid, pid, NULL);

		if (!this->handle) {
			printf("unable to open device\n");
			return false;
		}

		// Set the hid_read() function to be non-blocking.
		hid_set_nonblocking(this->handle, 1);

		this->available = true;
		
		return true;
	}

	std::wstring s2ws(const std::string& s)
	{
		int len;
		int slength = (int)s.length() + 1;
		len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
		wchar_t* buf = new wchar_t[len];
		MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
		std::wstring r(buf);
		delete[] buf;
		return r;
	}
	
	void beginRecord()
	{
		string filePrefix = "powermeter-";
		string fileTimestamp = to_string(st.wYear);
		fileTimestamp += "-";
		fileTimestamp += to_string(st.wMonth);
		fileTimestamp += "-";
		fileTimestamp += to_string(st.wDay);
		fileTimestamp += "_";
		fileTimestamp += to_string(st.wHour);
		fileTimestamp += "-";
		fileTimestamp += to_string(st.wMinute);
		fileTimestamp += "-";
		fileTimestamp += to_string(st.wSecond);
		string fileSuffix = ".csv";
		string fileName = filePrefix + fileTimestamp;
		fileName += fileSuffix;
		this->saveFile.open(fileName);
		bool fileOpened = this->saveFile.is_open();


		#define BUFSIZE 4096
		TCHAR  buffer[BUFSIZE] = TEXT("");
		TCHAR  buf[BUFSIZE] = TEXT("");
		TCHAR** lppPart = { NULL };

		std::wstring stemp = s2ws(fileName);
		LPCWSTR wFileName = stemp.c_str();

		DWORD retval = GetFullPathName(wFileName,
			BUFSIZE,
			buffer,
			lppPart);

		if (retval == 0)
		{
			// Handle an error condition.
			//printf("GetFullPathName failed (%d)\n", GetLastError());
			//return;
		}
		else
		{
			_tprintf(TEXT("The full path name is:  %s\n"), buffer);
			if (lppPart != NULL && *lppPart != 0)
			{
				//_tprintf(TEXT("The final component in the path name is:  %s\n"), *lppPart);
			}
		}
		int i = 0;
	}

	void endRecord()
	{
		this->saveFile.close();
	}
};
UsbPowerMeter upm;

INT_PTR WINAPI WinProcCallback(
	HWND hWnd,
	UINT message,
	WPARAM wParam,
	LPARAM lParam
)
// Routine Description:
//     Simple Windows callback for handling messages.
//     This is where all the work is done because the example
//     is using a window to process messages. This logic would be handled 
//     differently if registering a service instead of a window.

// Parameters:
//     hWnd - the window handle being registered for events.

//     message - the message being interpreted.

//     wParam and lParam - extended information provided to this
//          callback by the message sender.

//     For more information regarding these parameters and return value,
//     see the documentation for WNDCLASSEX and CreateWindowEx.
{
	//Sleep(30);
	LRESULT lRet = 1;
	static HDEVNOTIFY hDeviceNotify;
	static HWND hEditWnd;
	static ULONGLONG msgCount = 0;

	switch (message)
	{
	case WM_CREATE:
		//
		// This is the actual registration., In this example, registration 
		// should happen only once, at application startup when the window
		// is created.
		//
		// If you were using a service, you would put this in your main code 
		// path as part of your service initialization.
		//
		if (!DoRegisterDeviceInterfaceToHwnd(
			WceusbshGUID,
			hWnd,
			&hDeviceNotify))
		{
			// Terminate on failure.
			ErrorHandler(L"DoRegisterDeviceInterfaceToHwnd");
			ExitProcess(1);
		}


		//
		// Make the child window for output.
		//
		hEditWnd = CreateWindow(TEXT("EDIT"),// predefined class 
			NULL,        // no window title 
			WS_CHILD | WS_VISIBLE | WS_VSCROLL |
			ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
			0, 0, 0, 0,  // set size in WM_SIZE message 
			hWnd,        // parent window 
			(HMENU)1,    // edit control ID 
			(HINSTANCE)GetWindowLong(hWnd, GWL_HINSTANCE),
			NULL);       // pointer not needed 

		if (hEditWnd == NULL)
		{
			// Terminate on failure.
			ErrorHandler(L"CreateWindow: Edit Control");
			ExitProcess(1);
		}
		// Add text to the window. 
		SendMessage(hEditWnd, WM_SETTEXT, 0,
			(LPARAM)TEXT("Registered for USB device notification...\n"));

		break;

	/*case WM_SETFOCUS:
		SetFocus(hEditWnd);

		break;*/

	case WM_SIZE:
		// Make the edit control the size of the window's client area. 
		MoveWindow(hEditWnd,
			0, 0,                  // starting x- and y-coordinates 
			LOWORD(lParam),        // width of client area 
			HIWORD(lParam),        // height of client area 
			TRUE);                 // repaint window 

		break;

	case WM_DEVICECHANGE:
	{
		//
		// This is the actual message from the interface via Windows messaging.
		// This code includes some additional decoding for this particular device type
		// and some common validation checks.
		//
		// Note that not all devices utilize these optional parameters in the same
		// way. Refer to the extended information for your particular device type 
		// specified by your GUID.
		//
		PDEV_BROADCAST_DEVICEINTERFACE b = (PDEV_BROADCAST_DEVICEINTERFACE)lParam;
		TCHAR strBuff[256];

		// Output some messages to the window.
		switch (wParam)
		{
		case DBT_DEVICEARRIVAL:
			msgCount++;
			StringCchPrintf(
				strBuff, 256,
				TEXT("Message %d: DBT_DEVICEARRIVAL\n"), (int)msgCount);
			upm.begin(0x2341, 0x8036);
			break;
		case DBT_DEVICEREMOVECOMPLETE:
			msgCount++;
			StringCchPrintf(
				strBuff, 256,
				TEXT("Message %d: DBT_DEVICEREMOVECOMPLETE\n"), (int)msgCount);
			upm.available = false;
			break;
		case DBT_DEVNODES_CHANGED:
			msgCount++;
			StringCchPrintf(
				strBuff, 256,
				TEXT("Message %d: DBT_DEVNODES_CHANGED\n"), (int)msgCount);
			break;
		default:
			msgCount++;
			StringCchPrintf(
				strBuff, 256,
				TEXT("Message %d: WM_DEVICECHANGE message received, value %d unhandled.\n"),
				(int)msgCount, wParam);
			break;
		}
		OutputMessage(hEditWnd, wParam, (LPARAM)strBuff);
	}
	break;
	case WM_CLOSE:
		if (!UnregisterDeviceNotification(hDeviceNotify))
		{
			ErrorHandler(L"UnregisterDeviceNotification");
		}
		DestroyWindow(hWnd);
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	default:
		// Send all other messages on to the default windows handler.
		lRet = DefWindowProc(hWnd, message, wParam, lParam);
		break;
	}

	return lRet;
}

#define WND_CLASS_NAME TEXT("SampleAppWindowClass")

BOOL InitWindowClass()
// Routine Description:
//      Simple wrapper to initialize and register a window class.

// Parameters:
//     None

// Return Value:
//     TRUE on success, FALSE on failure.

// Note: 
//     wndClass.lpfnWndProc and wndClass.lpszClassName are the
//     important unique values used with CreateWindowEx and the
//     Windows message pump.
{
	WNDCLASSEX wndClass;

	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	wndClass.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandle(0));
	wndClass.lpfnWndProc = reinterpret_cast<WNDPROC>(WinProcCallback);
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hIcon = LoadIcon(0, IDI_APPLICATION);
	wndClass.hbrBackground = CreateSolidBrush(RGB(192, 192, 192));
	wndClass.hCursor = LoadCursor(0, IDC_ARROW);
	wndClass.lpszClassName = WND_CLASS_NAME;
	wndClass.lpszMenuName = NULL;
	wndClass.hIconSm = wndClass.hIcon;


	if (!RegisterClassEx(&wndClass))
	{
		ErrorHandler(L"RegisterClassEx");
		return FALSE;
	}
	return TRUE;
}

////////////////////////////////
//End Windows Event Pump Stuff
////////////////////////////////

BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
		printf("[Ctrl]+C\n");
		isRunning = false;
		// Signal is handled - don't pass it on to the next handler
		return TRUE;
	default:
		// Pass signal on to the next handler
		return FALSE;
	}
}


DWORD WINAPI updateUPM(LPVOID lpParameter)
{
	while(true)
	{
		if (upm.available && !upm.paused)
		{
			auto start = high_resolution_clock::now();
			UsbPowerMeter& upm = *((UsbPowerMeter*)lpParameter);
			float current;
			float voltage;
			float power;
			unsigned long arduinoTime;

			unsigned char buf[256];

			memset(buf, 0x00, sizeof(buf));
			buf[0] = 0x01;
			buf[1] = 0x81;

			int res = 0;
			while (res == 0) {
				res = hid_read(upm.handle, buf, sizeof(buf));
				if (res == 0)
					//printf("waiting...\n");
					if (res < 0)
						printf("Unable to read()\n");
			}

			unsigned char b[] = { buf[0], buf[1], buf[2], buf[3] };
			memcpy(&current, &b, sizeof(current));
			upm.current = current;

			unsigned char c[] = { buf[4], buf[5], buf[6], buf[7] };
			memcpy(&voltage, &c, sizeof(voltage));
			upm.voltage = voltage;

			unsigned char d[] = { buf[8], buf[9], buf[10], buf[11] };
			memcpy(&power, &d, sizeof(power));
			upm.power = power;

			unsigned char e[] = { buf[12], buf[13], buf[14], buf[15] };
			memcpy(&arduinoTime, &e, sizeof(arduinoTime));
			upm.milis = arduinoTime;

			auto stop = high_resolution_clock::now();
			auto duration = duration_cast<microseconds>(stop - start);
			upm.totalTime = duration.count();
			if(upm.record)
			{
				GetSystemTime(&st);
				string outLine = to_string(current);
				outLine += ",";
				outLine += to_string(voltage);
				outLine += ",";
				outLine += to_string(power);
				outLine += ",";
				string fileTimestamp = to_string(st.wYear);
				fileTimestamp += "-";
				fileTimestamp += to_string(st.wMonth);
				fileTimestamp += "-";
				fileTimestamp += to_string(st.wDay);
				fileTimestamp += "_";
				fileTimestamp += to_string(st.wHour);
				fileTimestamp += ";";
				fileTimestamp += to_string(st.wMinute);
				fileTimestamp += ";";
				fileTimestamp += to_string(st.wSecond);
				fileTimestamp += ".";
				fileTimestamp += to_string(st.wMilliseconds);
				outLine += fileTimestamp;
				outLine += "\n";
				upm.saveFile << outLine;
			}
		}else
		{
			Sleep(500);
		}
	}
}

DWORD WINAPI updateConsole(LPVOID lpParameter)
{
	const char* const statusMessage = "Test Program";

	SetConsoleCtrlHandler(HandlerRoutine, TRUE);

	//First, enable VT mode
	bool consoleResult = AllocConsole();
	freopen("CON", "w", stdout);
	freopen("CON", "w", stderr);
	freopen("CON", "r", stdin); // Note: "r", not "w".
	bool fSuccess = EnableVTMode();
	if (!fSuccess)
	{
		printf("Unable to enter VT processing mode. Quitting.\n");
		return -1;
	}
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE)
	{
		printf("Couldn't get the console handle. Quitting.\n");
		return -1;
	}

	CONSOLE_SCREEN_BUFFER_INFO ScreenBufferInfo;
	GetConsoleScreenBufferInfo(hOut, &ScreenBufferInfo);

	COORD Size;
	Size.X = ScreenBufferInfo.srWindow.Right - ScreenBufferInfo.srWindow.Left + 1;
	Size.Y = ScreenBufferInfo.srWindow.Bottom - ScreenBufferInfo.srWindow.Top + 1;

	COORD NewSize;

	//Soft reset the terminal
	printf(CSI "!p");

	// Enter the alternate buffer
	printf(CSI "?1049h");

	printf(OSC "2;Power Meter");

	//Hide cursor
	printf(CSI "?25l");

	PrintStatusLine(statusMessage, Size);

	float lastCurrent = 0;
	float lastVoltage = 0;
	float lastPower = 0;

	DWORD cc;
	INPUT_RECORD irec;
	HANDLE h = GetStdHandle(STD_INPUT_HANDLE);

	bool updateConsoleLoop = TRUE;
	
	while (true)
	{
		
		DWORD keyEventCount;
		if(GetNumberOfConsoleInputEvents(h, &keyEventCount) && keyEventCount > 0)
		{
			KEY_EVENT_RECORD krec;
			ReadConsoleInput(h, &irec, 1, &cc);
			if (irec.EventType == KEY_EVENT
				&& ((KEY_EVENT_RECORD&)irec.Event).bKeyDown
				)//&& ! ((KEY_EVENT_RECORD&)irec.Event).wRepeatCount )
			{
				krec = (KEY_EVENT_RECORD&)irec.Event;
				if (toupper(krec.uChar.AsciiChar) == 'P')
				{
					updateConsoleLoop = !updateConsoleLoop;
					upm.paused = !upm.paused;
				}else if(toupper(krec.uChar.AsciiChar) == 'R')
				{
					if(upm.record == false)
					{
						upm.beginRecord();
						upm.record = true;
					}else
					{
						upm.endRecord();
					}
				}
			}
		}else if(updateConsoleLoop == false)
		{
			Sleep(500);
		}
		
		if(updateConsoleLoop)
		{
			GetConsoleScreenBufferInfo(hOut, &ScreenBufferInfo);
			NewSize.X = ScreenBufferInfo.srWindow.Right - ScreenBufferInfo.srWindow.Left + 1;
			NewSize.Y = ScreenBufferInfo.srWindow.Bottom - ScreenBufferInfo.srWindow.Top + 1;

			bool sizeChanged = (Size.X != NewSize.X) || (Size.Y != NewSize.Y);
			bool displayChanged = (upm.current != lastCurrent) || (upm.voltage != lastVoltage) || (upm.power != lastPower);

			if (displayChanged || sizeChanged)
			{
				if (sizeChanged) {
					Size.X = NewSize.X;
					Size.Y = NewSize.Y;
					//clear the terminal
					printf(CSI "J2");
					//Hide cursor
					printf(CSI "?25l");
					PrintStatusLine(statusMessage, Size);
				}

				moveTo(1, 0);
				clearLine();
				printf("%fmA", upm.current);
				moveTo(2, 0);
				clearLine();
				printf("%fmV", upm.voltage);
				moveTo(3, 0);
				clearLine();
				printf("%fmW", upm.power);
				moveTo(4, 0);
				clearLine();
				printf("%duS", upm.totalTime);

				lastCurrent = upm.current;
				lastVoltage = upm.voltage;
				lastPower = upm.power;
			}
		}
	}
}

int __stdcall _tWinMain(_In_ HINSTANCE hInstanceExe,
	_In_opt_ HINSTANCE, // should not reference this parameter
	_In_ PTSTR lpstrCmdLine,
	_In_ int nCmdShow)
{
	/////////////////////
	//Window Stuff Begin
	/////////////////////
	//
	// To enable a console project to compile this code, set
	// Project->Properties->Linker->System->Subsystem: Windows.
	//

	int nArgC = 0;
	PWSTR* ppArgV = CommandLineToArgvW(lpstrCmdLine, &nArgC);
	g_pszAppName = ppArgV[0];

	if (!InitWindowClass())
	{
		// InitWindowClass displays any errors
		return -1;
	}

	// Main app window

	HWND hWnd = CreateWindowEx(
		WS_EX_CLIENTEDGE | WS_EX_APPWINDOW,
		WND_CLASS_NAME,
		g_pszAppName,
		WS_OVERLAPPEDWINDOW, // style
		CW_USEDEFAULT, 0,
		640, 480,
		NULL, NULL,
		hInstanceExe,
		NULL);

	if (hWnd == NULL)
	{
		ErrorHandler(L"CreateWindowEx: main appwindow hWnd");
		return -1;
	}

	// Actually draw the window.

	ShowWindow(hWnd, SW_SHOWNORMAL);
	UpdateWindow(hWnd);

	/////////////////////
	//Usb Stuff Begin
	/////////////////////

	int res;
	unsigned char buf[256];
	constexpr auto MAX_STR = 255;

	if (hid_init())
		return -1;

	memset(buf, 0x00, sizeof(buf));
	buf[0] = 0x01;
	buf[1] = 0x81;

	/////////////////////
	//Usb Stuff End
	/////////////////////

	/////////////////////
	//Begin Program Loop
	/////////////////////
	upm.begin(0x2341, 0x8036);

	DWORD updateUpmThreadID;
	HANDLE mHandle = CreateThread(0, 0, updateUPM, &upm, 0, &updateUpmThreadID);

	DWORD updateConsoleThreadID;
	HANDLE updateConsoleHandle = CreateThread(0, 0, updateConsole, &upm, 0, &updateConsoleThreadID);

	while (isRunning) {
		GetSystemTime(&st);
		
		MSG msg;
		//int retVal = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
		int retVal = GetMessage(&msg, NULL, 0, 0);

		if (retVal == -1)
		{
			ErrorHandler(L"GetMessage");
			break;
		}
		else
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	/////////////////////
	//End Program Loop
	/////////////////////

	/////////////////////
	//Begin Cleanup
	/////////////////////

	// Exit the alternate buffer
	printf(CSI "?1049l");
	hid_close(upm.handle);
	CloseHandle(mHandle);
	CloseHandle(updateConsoleHandle);

	// Free static HIDAPI objects.
	hid_exit();
	return 0;

	/////////////////////
	//End Cleanup
	/////////////////////
}
