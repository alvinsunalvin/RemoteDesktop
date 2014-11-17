#include "stdafx.h"
#include "Client.h"
#include "Console.h"
#include "ImageCompression.h"
#include "CommonNetwork.h"
#include "Display.h"
#include "BaseClient.h"
#include "..\RemoteDesktop_Library\SocketHandler.h"

RemoteDesktop::Client::Client(HWND hwnd) : _HWND(hwnd) {

#if defined _DEBUG
	_DebugConsole = std::make_unique<CConsole>();
#endif
	_ImageCompression = std::make_unique<ImageCompression>();
	_Display = std::make_unique<Display>(hwnd);
	SetWindowText(_HWND, L"Remote Desktop Viewer");
}

RemoteDesktop::Client::~Client(){
	DEBUG_MSG("~Client");
}
void RemoteDesktop::Client::OnDisconnect(std::shared_ptr<SocketHandler>& sh){
	SetWindowText(_HWND, L"Remote Desktop Viewer");
}
void RemoteDesktop::Client::Connect(std::wstring host, std::wstring port){
	_NetworkClient = std::make_unique<BaseClient>(std::bind(&RemoteDesktop::Client::OnConnect, this, std::placeholders::_1),
		std::bind(&RemoteDesktop::Client::OnReceive, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
		std::bind(&RemoteDesktop::Client::OnDisconnect, this, std::placeholders::_1));
	std::wstring str = L"Attemping Connect to: ";
	str += host;
	str += L":";
	str += port;
	SetWindowText(_HWND, str.c_str());
	_Host = host;
	_Port = port;
	_NetworkClient->Connect(host, port);
}
void RemoteDesktop::Client::Stop(){
	_NetworkClient->Stop();
}

bool RemoteDesktop::Client::SetCursor(){
	return _Display->SetCursor();
}

void RemoteDesktop::Client::OnConnect(std::shared_ptr<SocketHandler>& sh){
	DEBUG_MSG("Connection Successful");
	std::wstring str = L"Connected to: ";
	str += _Host;
	str += L":";
	str += _Port;
	SetWindowText(_HWND, str.c_str());
	_DownKeys.clear();
}

void RemoteDesktop::Client::KeyEvent(int VK, bool down) {
	if (down){
		if (std::find(_DownKeys.begin(), _DownKeys.end(), VK) != _DownKeys.end()) _DownKeys.push_back(VK);// key is not in a down state
	}
	else {//otherwise, remove the key
		std::remove(_DownKeys.begin(), _DownKeys.end(), VK);
	}

	NetworkMsg msg;
	KeyEvent_Header h;
	h.VK = VK;
	h.down = down == true ? 0 : -1;
	DEBUG_MSG("KeyEvent % in state, down %", VK, (int)h.down);
	msg.push_back(h);
	_NetworkClient->Send(NetworkMessages::KEYEVENT, msg);
}
void RemoteDesktop::Client::MouseEvent(unsigned int action, int x, int y, int wheel){
	NetworkMsg msg;
	MouseEvent_Header h;
	static MouseEvent_Header _LastMouseEvent;
	h.HandleID = 0;
	h.Action = action;
	h.pos.left = x;
	h.pos.top = y;
	h.wheel = wheel;

	if (_LastMouseEvent.Action == action && _LastMouseEvent.pos.left == x && _LastMouseEvent.pos.top == y && wheel == 0) DEBUG_MSG("skipping mouse event, duplicate");
	else {
		memcpy(&_LastMouseEvent, &h, sizeof(h));
		msg.push_back(h);
		_NetworkClient->Send(NetworkMessages::MOUSEEVENT, msg);
	}

}
void RemoteDesktop::Client::SendCAD(){
	NetworkMsg msg;
	_NetworkClient->Send(NetworkMessages::CAD, msg);
}

void RemoteDesktop::Client::Draw(HDC hdc){
	_Display->Draw(hdc);
}

void RemoteDesktop::Client::OnReceive(Packet_Header* header, const char* data, std::shared_ptr<SocketHandler>& sh) {
	auto t = Timer(true);
	auto beg = data;
	if (header->Packet_Type == NetworkMessages::RESOLUTIONCHANGE){

		Image img;
		memcpy(&img.height, beg, sizeof(img.height));
		beg += sizeof(img.height);
		memcpy(&img.width, beg, sizeof(img.width));
		beg += sizeof(img.width);
		img.compressed = true;
		img.data = (unsigned char*)beg;
		img.size_in_bytes = header->PayloadLen - sizeof(img.height) - sizeof(img.width);

		_Display->NewImage(_ImageCompression->Decompress(img));

	}
	else if (header->Packet_Type == NetworkMessages::UPDATEREGION){
		Image img;
		Image_Diff_Header imgdif_network;

		memcpy(&imgdif_network, beg, sizeof(imgdif_network));

		beg += sizeof(imgdif_network);
		img.height = imgdif_network.rect.height;
		img.width = imgdif_network.rect.width;
		img.compressed = imgdif_network.compressed == 0 ? true : false;
		img.data = (unsigned char*)beg;
		img.size_in_bytes = header->PayloadLen - sizeof(imgdif_network);

		_Display->UpdateImage(_ImageCompression->Decompress(img), imgdif_network);

	}
	else if (header->Packet_Type == NetworkMessages::MOUSEEVENT){
		MouseEvent_Header h;
		memcpy(&h, beg, sizeof(h));
		_Display->UpdateMouse(h);
	}
	static int updatecounter = 0;
	if (updatecounter++ > 20){
		std::wstring str = L"Connected to: ";
		str += _Host;
		str += L":";
		str += _Port;
		str += L" Send: ";
		str += FormatBytes(sh->Traffic.get_SendBPS());
		str += L" Recv: ";
		str += FormatBytes(sh->Traffic.get_RecvBPS());
		SetWindowText(_HWND, str.c_str());
		updatecounter = 0;
	}


	t.Stop();
	//DEBUG_MSG("took: %", t.Elapsed());
}

