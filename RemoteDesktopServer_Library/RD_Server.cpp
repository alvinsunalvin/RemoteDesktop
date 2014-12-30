#include "stdafx.h"
#include "RD_Server.h"
#include "MouseCapture.h"
#include "Desktop_Monitor.h"
#include "BaseServer.h"
#include "SocketHandler.h"
#include "Rect.h"
#include "..\RemoteDesktop_Library\CommonNetwork.h"
#include "..\RemoteDesktop_Library\Handle_Wrapper.h"
#include "..\RemoteDesktop_Library\Clipboard_Monitor.h"
#include "..\RemoteDesktop_Library\Delegate.h"
#include "..\RemoteDesktopServer_Library\SystemTray.h"
#include "..\RemoteDesktop_Library\Desktop_Capture_Container.h"

#include "..\RemoteDesktop_Library\NetworkSetup.h"
#include "..\RemoteDesktop_Library\Utilities.h"
#include "Lmcons.h"

#if _DEBUG
#include "Console.h"
#endif

#define FRAME_CAPTURE_INTERVAL 100 //ms between checking for screen changes
#define SELF_REMOVE_STRING  TEXT("cmd.exe /C ping 1.1.1.1 -n 1 -w 3000 > Nul & Del \"%s\"")


void DeleteMe(){

	TCHAR szModuleName[MAX_PATH];
	TCHAR szCmd[2 * MAX_PATH];
	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };

	GetModuleFileName(NULL, szModuleName, MAX_PATH);

	StringCbPrintf(szCmd, 2 * MAX_PATH, SELF_REMOVE_STRING, szModuleName);

	CreateProcess(NULL, szCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

}


RemoteDesktop::RD_Server::RD_Server() :
_CADEventHandle(RAIIHANDLE(OpenEvent(EVENT_MODIFY_STATE, FALSE, L"Global\\SessionEventRDCad"))),
_SelfRemoveEventHandle(RAIIHANDLE(OpenEvent(EVENT_MODIFY_STATE, FALSE, L"Global\\SessionEventRemoveSelf")))
{

#if _DEBUG
	_DebugConsole = std::make_unique<CConsole>();
#endif

	DWORD bufsize = (UNLEN + 1);
	char buff[UNLEN + 1];
	GetUserNameA(buff, &bufsize);
	std::string uname = buff;
	_RunningAsService = find_substr(uname, std::string("system")) != -1;

	mousecapturing = std::make_unique<MouseCapture>();
	_DesktopMonitor = std::make_unique<DesktopMonitor>();
	_NetworkServer = std::make_unique<BaseServer>(
		DELEGATE(&RemoteDesktop::RD_Server::OnConnect, this),
		DELEGATE(&RemoteDesktop::RD_Server::OnReceive, this),
		DELEGATE(&RemoteDesktop::RD_Server::OnDisconnect, this));
	_ClipboardMonitor = std::make_unique<ClipboardMonitor>(DELEGATE(&RemoteDesktop::RD_Server::_OnClipboardChanged, this));
	_SystemTray = std::make_unique<SystemTray>();
	_SystemTray->Start(DELEGATE(&RemoteDesktop::RD_Server::_CreateSystemMenu, this));
}
RemoteDesktop::RD_Server::~RD_Server(){

	if (_RemoveOnExit) {
		if (_SelfRemoveEventHandle.get() != nullptr) {
			SetEvent(_SelfRemoveEventHandle.get());//signal the self removal process 
		}
		else DeleteMe();//try a self removal 
	}
}
void RemoteDesktop::RD_Server::_CreateSystemMenu(){
	_SystemTray->AddMenuItem(L"Exit", DELEGATE(&RemoteDesktop::RD_Server::_TriggerShutDown, this));
	_SystemTray->AddMenuItem(L"Exit and Remove", DELEGATE(&RemoteDesktop::RD_Server::_TriggerShutDown_and_RemoveSelf, this));

}

void RemoteDesktop::RD_Server::_TriggerShutDown(){
	_NetworkServer->GracefulStop();//this will cause the main loop to stop and the program to exit
}
void RemoteDesktop::RD_Server::_TriggerShutDown_and_RemoveSelf(){
	_TriggerShutDown();
	_RemoveOnExit = true;
}

void RemoteDesktop::RD_Server::_Handle_DisconnectandRemove(Packet_Header* header, const char* data, std::shared_ptr<RemoteDesktop::SocketHandler>& sh){
	_TriggerShutDown_and_RemoveSelf();
}

void RemoteDesktop::RD_Server::_Handle_Settings(Packet_Header* header, const char* data, std::shared_ptr<RemoteDesktop::SocketHandler>& sh){
	Settings_Header h;
	assert(header->PayloadLen >= sizeof(h));
	memcpy(&h, data, sizeof(h));
	Image_Settings::GrazyScale = h.GrayScale;
	Image_Settings::Quality = h.Image_Quality;
	_ClipboardMonitor->set_ShareClipBoard(h.ShareClip);

	//DEBUG_MSG("Setting Quality to % and GrayScale to %", q, g);
}
void RemoteDesktop::RD_Server::_OnClipboardChanged(const Clipboard_Data& c){
	NetworkMsg msg;
	int dibsize(c.m_pDataDIB.size()), htmlsize(c.m_pDataHTML.size()), rtfsize(c.m_pDataRTF.size()), textsize(c.m_pDataText.size());

	msg.push_back(dibsize);
	msg.data.push_back(DataPackage(c.m_pDataDIB.data(), dibsize));
	msg.push_back(htmlsize);
	msg.data.push_back(DataPackage(c.m_pDataHTML.data(), htmlsize));
	msg.push_back(rtfsize);
	msg.data.push_back(DataPackage(c.m_pDataRTF.data(), rtfsize));
	msg.push_back(textsize);
	msg.data.push_back(DataPackage(c.m_pDataText.data(), textsize));

	_NetworkServer->SendToAll(NetworkMessages::CLIPBOARDCHANGED, msg);
}
void RemoteDesktop::RD_Server::_Handle_ClipBoard(Packet_Header* header, const char* data, std::shared_ptr<RemoteDesktop::SocketHandler>& sh){
	Clipboard_Data clip;
	int dibsize(0), htmlsize(0), rtfsize(0), textsize(0);

	dibsize = (*(int*)data);
	data += sizeof(dibsize);
	clip.m_pDataDIB.resize(dibsize);
	memcpy(clip.m_pDataDIB.data(), data, dibsize);
	data += dibsize;

	htmlsize = (*(int*)data);
	data += sizeof(htmlsize);
	clip.m_pDataHTML.resize(htmlsize);
	memcpy(clip.m_pDataHTML.data(), data, htmlsize);
	data += htmlsize;

	rtfsize = (*(int*)data);
	data += sizeof(rtfsize);
	clip.m_pDataRTF.resize(rtfsize);
	memcpy(clip.m_pDataRTF.data(), data, rtfsize);
	data += rtfsize;

	textsize = (*(int*)data);
	data += sizeof(textsize);
	clip.m_pDataText.resize(textsize);
	memcpy(clip.m_pDataText.data(), data, textsize);
	data += textsize;
	_ClipboardMonitor->Restore(clip);
}


void RemoteDesktop::RD_Server::OnConnect(std::shared_ptr<SocketHandler>& sh){
	std::lock_guard<std::mutex> lock(_NewClientLock);
	_NewClients.push_back(sh);
	DEBUG_MSG("New Client OnConnect");
}
void _HandleKeyEvent(RemoteDesktop::Packet_Header* header, const char* data, std::shared_ptr<RemoteDesktop::SocketHandler>& sh){
	RemoteDesktop::KeyEvent_Header h;
	assert(header->PayloadLen >= sizeof(h));
	memcpy(&h, data, sizeof(h));
	INPUT inp;

	inp.type = INPUT_KEYBOARD;
	inp.ki.wVk = h.VK;
	inp.ki.dwFlags = h.down == 0 ? 0 : KEYEVENTF_KEYUP;
	SendInput(1, &inp, sizeof(INPUT));
}

void  RemoteDesktop::RD_Server::_Handle_MouseUpdate(Packet_Header* header, const char* data, std::shared_ptr<SocketHandler>& sh){
	MouseEvent_Header h;
	assert(header->PayloadLen == sizeof(h));
	memcpy(&h, data, sizeof(h));
	mousecapturing->Last_ScreenPos = mousecapturing->Current_ScreenPos = h.pos;
	INPUT inp;
	memset(&inp, 0, sizeof(inp));
	inp.type = INPUT_MOUSE;
	inp.mi.mouseData = 0;

	auto scx = (float)GetSystemMetrics(SM_CXSCREEN);
	auto scy = (float)GetSystemMetrics(SM_CYSCREEN);

	auto divl = (float)h.pos.left;
	auto divt = (float)h.pos.top;
	inp.mi.dx = (LONG)((65536.0f / scx)*divl);//x being coord in pixels
	inp.mi.dy = (LONG)((65536.0f / scy)*divt);//y being coord in pixels
	if (h.Action == WM_MOUSEMOVE) inp.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
	else if (h.Action == WM_LBUTTONDOWN) inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
	else if (h.Action == WM_LBUTTONUP) inp.mi.dwFlags = MOUSEEVENTF_LEFTUP;
	else if (h.Action == WM_RBUTTONDOWN) inp.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
	else if (h.Action == WM_RBUTTONUP) inp.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
	else if (h.Action == WM_MBUTTONDOWN) inp.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
	else if (h.Action == WM_MBUTTONUP) inp.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
	else if (h.Action == WM_MOUSEWHEEL) {
		inp.mi.dwFlags = MOUSEEVENTF_WHEEL;
		inp.mi.mouseData = h.wheel;
	}
	if (h.Action == WM_LBUTTONDBLCLK){
		inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;
		//mouse_event(inp.mi.dwFlags, inp.mi.dx, inp.mi.dy, 0, 0);
		SendInput(1, &inp, sizeof(inp));
	}
	else if (h.Action == WM_RBUTTONDBLCLK){
		inp.mi.dwFlags = MOUSEEVENTF_RIGHTUP | MOUSEEVENTF_RIGHTDOWN;
		//mouse_event(inp.mi.dwFlags, inp.mi.dx, inp.mi.dy, 0, 0);
		SendInput(1, &inp, sizeof(inp));
	}
	//DEBUG_MSG("GOt here");
	//mouse_event(inp.mi.dwFlags, inp.mi.dx, inp.mi.dy, inp.mi.mouseData, 0);
	SendInput(1, &inp, sizeof(inp));
}
void RemoteDesktop::RD_Server::_Handle_File(RemoteDesktop::Packet_Header* header, const char* data, std::shared_ptr<RemoteDesktop::SocketHandler>& sh){
	File_Header fh;
	assert(header->PayloadLen > sizeof(fh));
	memcpy(&fh, data, sizeof(fh));
	data += sizeof(fh);
	std::string fname(fh.RelativePath);
	std::string path = "c:\\users\\" + _DesktopMonitor->get_ActiveUser() + "\\desktop\\" + fname;

	DEBUG_MSG("% BEG FILE: %", path.size(), path);
	int openoptions = std::ios::binary;
	if (fh.ID == 0) openoptions |= std::ios::trunc;// erase everything in the file 
	else openoptions |= std::ios::app;// append data 
	std::ofstream f(path, openoptions);
	f.write(data, fh.ChunkSize);
	DEBUG_MSG("% END FILE: %", path.size(), path);

}

void RemoteDesktop::RD_Server::_Handle_Folder(Packet_Header* header, const char* data, std::shared_ptr<RemoteDesktop::SocketHandler>& sh){
	unsigned char size = (unsigned char)*data;
	data++;
	std::string fname(data, size);
	data += size;
	std::string path = "c:\\users\\" + _DesktopMonitor->get_ActiveUser() + "\\desktop\\" + fname;
	DEBUG_MSG("% BEG FOLDER: %", path.size(), path);
	CreateDirectoryA(path.c_str(), NULL);
	DEBUG_MSG("% END FOLDER: %", path.size(), path);
}
void RemoteDesktop::RD_Server::_Handle_ConnectionInfo(Packet_Header* header, const char* data, std::shared_ptr<RemoteDesktop::SocketHandler>& sh){
	ConnectionInfo_Header h;
	assert(header->PayloadLen == sizeof(h));
	memcpy(&h, data, header->PayloadLen);
	h.UserName[UNAMELEN] = 0;
	sh->UserName = std::wstring(h.UserName);
	auto con = sh->UserName + L" has connected to your machine . . .";
	_SystemTray->Popup(L"Connection Established", con.c_str(), 2000);
}
void RemoteDesktop::RD_Server::OnReceive(Packet_Header* header, const char* data, std::shared_ptr<SocketHandler>& sh) {
	switch (header->Packet_Type){
	case NetworkMessages::KEYEVENT:
		_HandleKeyEvent(header, data, sh);
		break;
	case NetworkMessages::MOUSEEVENT:
		_Handle_MouseUpdate(header, data, sh);
		break;
	case NetworkMessages::CAD:
		SetEvent(_CADEventHandle.get());
		break;
	case NetworkMessages::FILE:
		_Handle_File(header, data, sh);
		break;
	case NetworkMessages::FOLDER:
		_Handle_Folder(header, data, sh);
		break;
	case NetworkMessages::CLIPBOARDCHANGED:
		_Handle_ClipBoard(header, data, sh);
		break;
	case NetworkMessages::DISCONNECTANDREMOVE:
		_Handle_DisconnectandRemove(header, data, sh);
		break;
	case NetworkMessages::SETTINGS:
		_Handle_Settings(header, data, sh);
		break;
	case NetworkMessages::CONNECTIONINFO:
		_Handle_ConnectionInfo(header, data, sh);
		break;

	default:
		break;
	}
}
void RemoteDesktop::RD_Server::OnDisconnect(std::shared_ptr<SocketHandler>& sh) {
	auto con = sh->UserName + L" has Disconnected from your machine . . .";
	_SystemTray->Popup(L"Connection Disconnected", con.c_str(), 2000);
}

void RemoteDesktop::RD_Server::_HandleNewClients(Image& imgg){
	if (_NewClients.empty()) return;
	auto sendimg = imgg.Clone();

	sendimg.Compress();
	NetworkMsg msg;
	int sz[2];
	sz[0] = sendimg.Height;
	sz[1] = sendimg.Width;
	DEBUG_MSG("Servicing new Client %, %, %", sendimg.Height, sendimg.Width, sendimg.size_in_bytes());
	msg.data.push_back(DataPackage((char*)&sz, sizeof(int) * 2));
	msg.data.push_back(DataPackage((char*)sendimg.get_Data(), sendimg.size_in_bytes()));

	std::lock_guard<std::mutex> lock(_NewClientLock);

	for (auto& a : _NewClients){

		a->Send(NetworkMessages::RESOLUTIONCHANGE, msg);
	}
	_NewClients.clear();
}
bool RemoteDesktop::RD_Server::_HandleResolutionUpdates(Image& imgg, Image& _lastimg){
	bool reschange = (imgg.Height != _lastimg.Height || imgg.Width != _lastimg.Width) && (imgg.Height > 0 && _lastimg.Width > 0);
	//if there was a resolution change
	if (reschange){
		std::vector<char> tmpbuf;
		auto sendimg = imgg.Clone();
		sendimg.Compress();
		NetworkMsg msg;
		int sz[2];
		sz[0] = sendimg.Height;
		sz[1] = sendimg.Width;

		msg.data.push_back(DataPackage((char*)&sz, sizeof(int) * 2));
		msg.data.push_back(DataPackage((char*)sendimg.get_Data(), sendimg.size_in_bytes()));
		_NetworkServer->SendToAll(NetworkMessages::RESOLUTIONCHANGE, msg);
		return true;
	}
	return false;
}


void RemoteDesktop::RD_Server::_Handle_ScreenUpdates(Image& img, Rect& rect){
	if (rect.width > 0 && rect.height > 0){

		NetworkMsg msg;
		auto imgdif = Image::Copy(img, rect);
		imgdif.Compress();
		msg.push_back(rect);
		msg.data.push_back(DataPackage((char*)imgdif.get_Data(), imgdif.size_in_bytes()));

		//DEBUG_MSG("_Handle_ScreenUpdates %, %, %", rect.height, rect.width, imgdif.size_in_bytes);
		_NetworkServer->SendToAll(NetworkMessages::UPDATEREGION, msg);
	}

}
void RemoteDesktop::RD_Server::_Handle_MouseUpdates(const std::unique_ptr<MouseCapture>& mousecapturing){
	static auto begintimer = std::chrono::high_resolution_clock::now();

	mousecapturing->Update();
	if (mousecapturing->Last_ScreenPos != mousecapturing->Current_ScreenPos){//mouse pos is different
		if (mousecapturing->Last_Mouse == mousecapturing->Current_Mouse){//mouse icon is the same... only send on interval
			if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - begintimer).count() < 50) return;
		}
		begintimer = std::chrono::high_resolution_clock::now();

		NetworkMsg msg;
		MouseEvent_Header h;
		h.pos = mousecapturing->Current_ScreenPos;
		h.HandleID = mousecapturing->Current_Mouse;
		msg.push_back(h);
		_NetworkServer->SendToAll(NetworkMessages::MOUSEEVENT, msg);
		if (mousecapturing->Last_Mouse != mousecapturing->Current_Mouse) DEBUG_MSG("Sending mouse Iconchange %", mousecapturing->Current_Mouse);
		mousecapturing->Last_ScreenPos = mousecapturing->Current_ScreenPos;
		mousecapturing->Last_Mouse = mousecapturing->Current_Mouse;

	}
}


void RemoteDesktop::RD_Server::Listen(unsigned short port, std::wstring host, bool reverseconnecttoproxy) {
	_RunningReverseProxy = reverseconnecttoproxy;

	//switch to input desktop 
	_DesktopMonitor->Switch_to_Desktop(DesktopMonitor::Desktops::INPUT);
	_NetworkServer->StartListening(port, host);

	auto _LastImages(CaptureDesktops());


	auto shutdownhandle(RAIIHANDLE(OpenEvent(EVENT_ALL_ACCESS, FALSE, L"Global\\SessionEventRDProgram")));

	WaitForSingleObject(shutdownhandle.get(), 100);//call this to get the first signal from the service

	DWORD dwEvent;
	auto lastwaittime = FRAME_CAPTURE_INTERVAL;

	while (_NetworkServer->Is_Running()){
		if (shutdownhandle.get() == NULL) std::this_thread::sleep_for(std::chrono::milliseconds(lastwaittime));//sleep
		else {
			dwEvent = WaitForSingleObject(shutdownhandle.get(), lastwaittime);
			if (dwEvent == 0){
				_NetworkServer->GracefulStop();//stop program!
				break;
			}
		}
		auto t1 = Timer(true);

		if (_NetworkServer->Client_Count() <= 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));//sleep if there are no clients connected.
			continue;
		}

		if (!_DesktopMonitor->Is_InputDesktopSelected()) _DesktopMonitor->Switch_to_Desktop(DesktopMonitor::Desktops::INPUT);

		_Handle_MouseUpdates(mousecapturing);

		auto currentimages(CaptureDesktops());
		if (!currentimages.empty() && !_LastImages.empty()){
			for (auto i = 0; i < 1; i++){
				_HandleNewClients(currentimages[i]);
				if (!_HandleResolutionUpdates(currentimages[i], _LastImages[i])){
					auto rect = Image::Difference(_LastImages[i], currentimages[i]);
					_Handle_ScreenUpdates(currentimages[i], rect);
				}
			}
		}

		_LastImages = std::move(currentimages);
		t1.Stop();
		auto tim = (int)t1.Elapsed_milli();
		lastwaittime = FRAME_CAPTURE_INTERVAL - tim;
		if (lastwaittime < 0) lastwaittime = 0;
		DEBUG_MSG("Time for work... %", t1.Elapsed_milli());
	}

	_NetworkServer->ForceStop();//stop program!
}
