#include <Windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11")
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler")
#include <chrono>
#include <atomic>
#include <string>
#include "AntTweakBar.h"

namespace
{
	char additiveShader[] = "cbuffer lights:register(b0){float4 lightPos[16]; float4 lightColor[16]; float4 lightInt_[16];} cbuffer params:register(b1){float3 ambientColor; float a;} "
		"struct VSOUT{float4 spos : SV_POSITION; float4 pos : TEXCOORD0; float4 norm : NORMAL0;}; "
		"float4 main(VSOUT ip) : SV_Target { float3 tot = ambientColor; "
		"for(uint light=0 ; light<16 ; ++light) { "
		"float att = distance(ip.pos, lightPos[light]); att = max(1-(att/lightInt_[light].x), 0); float4 L = normalize(float4(lightPos[light].xyz, 1) - ip.pos); float diff = max(dot(ip.norm, L), 0); "
		"tot += (lightColor[light]*diff) * att; "
		"} return float4(tot, 1); }";
	char lightenShader[] = "cbuffer lights:register(b0){float4 lightPos[16]; float4 lightColor[16]; float4 lightInt_[16];} cbuffer params:register(b1){float3 ambientColor; float a;} "
		"struct VSOUT{float4 spos : SV_POSITION; float4 pos : TEXCOORD0; float4 norm : NORMAL0;}; "
		"float4 main(VSOUT ip) : SV_Target { float3 tot = ambientColor; "
		"for(uint light=0 ; light<16 ; ++light) { "
		"float att = distance(ip.pos, lightPos[light]); att = max(1-(att/lightInt_[light].x), 0); float4 L = normalize(float4(lightPos[light].xyz, 1) - ip.pos); float diff = max(dot(ip.norm, L), 0); "
		"float4 temp = (lightColor[light]*diff) * att; tot = float3(max(temp.x,tot.x), max(temp.y,tot.y), max(temp.z,tot.z)); "
		"} return float4(tot, 1); }";
	char lightenBlendShader[] = "cbuffer lights:register(b0){float4 lightPos[16]; float4 lightColor[16]; float4 lightInt_[16];} cbuffer params:register(b1){float3 ambientColor; float washoutDamper;} "
		"struct VSOUT{float4 spos : SV_POSITION; float4 pos : TEXCOORD0; float4 norm : NORMAL0;}; "
		"float4 main(VSOUT ip) : SV_Target { float3 tot = ambientColor; float maxAtt = washoutDamper; "
		"for(uint light=0 ; light<16 ; ++light) { "
		"float att = distance(ip.pos, lightPos[light]); att = max(1-(att/lightInt_[light].x), 0); float4 L = normalize(float4(lightPos[light].xyz, 1) - ip.pos); att *= max(dot(ip.norm, L), 0); "
		"float4 temp = (lightColor[light]); "
		"tot = lerp(tot, temp.xyz, att*(1/(maxAtt*2))); maxAtt = max(att, maxAtt); "
		"} return float4(tot, 1); }";
	char flatShader[] = "cbuffer lights:register(b0){float4 lightPos[16]; float4 lightColor[16]; float4 lightInt_[16];} cbuffer params:register(b1){float3 ambientColor; float a;} "
		"struct VSOUT{float4 spos : SV_POSITION; float4 pos : TEXCOORD0; float4 norm : NORMAL0;}; "
		"float4 main(VSOUT ip) : SV_Target { return float4(ambientColor, 1); }";

	char vertexShader[] = "cbuffer params : register(b0){float3 objPos; float objSize; matrix proj;} "
		"struct VSOUT{float4 pos : SV_POSITION; float4 pos2 : TEXCOORD0; float4 norm : NORMAL0;}; "
		"VSOUT main(float3 pos : POSITION, float3 norm : NORMAL) {VSOUT op; "
		"op.pos = mul(float4((pos*objSize) + objPos + float3(0,0,-10), 1), proj); op.pos2 = float4(pos*objSize, 1); op.norm = float4(norm, 0); "
		"return op;}";

	// vars
	std::atomic_bool winresize = ATOMIC_VAR_INIT(true);
#define PI 3.14159f

	// structs
	struct Vec4
	{
		float x, y, z, a;
	};

	struct PixelParams
	{
		Vec4 ambientColor;
	};

	struct LightsParams
	{
		Vec4 Pos[16];
		Vec4 Color[16];
		Vec4 Intencity_[16];
	};

	struct VertexParams
	{
		Vec4 objPos;
		float projMatrix[16];
	};
}

bool GetBlob(char *shader, char *type, ID3DBlob **blob)
{
	ID3DBlob *errors = nullptr;
	HRESULT hr = D3DCompile(shader, std::strlen(shader), nullptr, nullptr, nullptr, "main", type, 0, 0, blob, &errors);
	if(FAILED(hr) && errors != nullptr)
	{
		std::string errstr(reinterpret_cast<char*>(errors->GetBufferPointer()));
		DebugBreak();
	}
	if(errors)
		errors->Release();
	return hr == S_OK;
}

LRESULT WINAPI WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if(TwEventWin(hWnd, msg, wParam, lParam)) return 0;
	switch(msg)
	{
	case WM_DESTROY: PostMessage(hWnd, WM_QUIT, 0, 0); break;
	case WM_SIZE: if(wParam != SIZE_MINIMIZED) winresize = true; break;
	default: return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
	// make window
	WNDCLASSEX winClass = {sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WinProc, 0, 0, instance, LoadIcon(NULL, IDI_SHIELD), 
		LoadCursor(NULL, IDC_ARROW), (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, L"DX11LT", LoadIcon(NULL, IDI_SHIELD)};
	if(!RegisterClassEx(&winClass)) return 1;
	HWND winHwnd = CreateWindow(winClass.lpszClassName, L"Light Blend Tests", WS_OVERLAPPEDWINDOW, 0, 0, 800, 600, 
		nullptr, nullptr, instance, nullptr);
	if(!winHwnd) return 1;
	ShowWindow(winHwnd, SW_SHOWDEFAULT);

	// make dx
	D3D_FEATURE_LEVEL FL;
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *immediateContext = nullptr;
	IDXGISwapChain *swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {
		{800, 600, {0,0}, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED, DXGI_MODE_SCALING_UNSPECIFIED},
		{1, 0}, DXGI_USAGE_RENDER_TARGET_OUTPUT, 1, winHwnd, TRUE, DXGI_SWAP_EFFECT_DISCARD, 0};
	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr,
		0, D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device, &FL, &immediateContext );
	if(FAILED(hr)) return 1;

	// load dx stuff
	float cube[] = 
	{
		// front
		-1,  1,  1,  0,  0,  1,
		 1,  1,  1,  0,  0,  1,
		-1, -1,  1,  0,  0,  1,
		 1,  1,  1,  0,  0,  1,
		 1, -1,  1,  0,  0,  1,
		-1, -1,  1,  0,  0,  1,

		// back
		 1,  1, -1,  0,  0, -1,
		-1,  1, -1,  0,  0, -1,
		-1, -1, -1,  0,  0, -1,
		-1, -1, -1,  0,  0, -1,
		 1, -1, -1,  0,  0, -1,
		 1,  1, -1,  0,  0, -1,
		
		// left
		 1, -1, -1,  1,  0,  0,
		 1, -1,  1,  1,  0,  0,
		 1,  1,  1,  1,  0,  0,
		 1,  1,  1,  1,  0,  0,
		 1,  1, -1,  1,  0,  0,
		 1, -1, -1,  1,  0,  0,
		 
		// right
		-1,  1,  1, -1,  0,  0,
		-1, -1,  1, -1,  0,  0,
		-1, -1, -1, -1,  0,  0,
		-1, -1, -1, -1,  0,  0,
		-1,  1, -1, -1,  0,  0,
		-1,  1,  1, -1,  0,  0,
	};
	ID3D11Buffer *cubeBuff = nullptr;
	D3D11_BUFFER_DESC buffDesc = {sizeof(cube), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
	D3D11_SUBRESOURCE_DATA data = {&cube, 0, 0};
	hr = device->CreateBuffer(&buffDesc, &data, &cubeBuff);
	if(FAILED(hr)) return 1;

	D3D11_INPUT_ELEMENT_DESC vertexTypeLayout[] = 
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	};
	ID3DBlob *blob = nullptr;
	ID3D11VertexShader *vertexShaderBuffer = nullptr;
	ID3D11PixelShader *additivePixelShaderBuffer = nullptr, *lightenPixelShaderBuffer = nullptr, *flatPixelShaderBuffer = nullptr,
		*lightenBlendPixelShaderBuffer = nullptr;
	ID3D11InputLayout *inputLayoutBuffer = nullptr;
	if(!GetBlob(vertexShader, "vs_4_0", &blob)) return 1;
	hr = device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vertexShaderBuffer);
	if(FAILED(hr)) return 1;
	hr = device->CreateInputLayout(vertexTypeLayout, 2, blob->GetBufferPointer(), blob->GetBufferSize(), &inputLayoutBuffer);
	blob->Release(); blob = nullptr;
	if(FAILED(hr)) return 1;
	immediateContext->IASetInputLayout(inputLayoutBuffer);
	if(!GetBlob(additiveShader, "ps_4_0", &blob)) return 1;
	hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &additivePixelShaderBuffer);
	if(FAILED(hr)) return 1;
	if(!GetBlob(lightenShader, "ps_4_0", &blob)) return 1;
	hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &lightenPixelShaderBuffer);
	if(FAILED(hr)) return 1;
	if(!GetBlob(lightenBlendShader, "ps_4_0", &blob)) return 1;
	hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &lightenBlendPixelShaderBuffer);
	if(FAILED(hr)) return 1;
	if(!GetBlob(flatShader, "ps_4_0", &blob)) return 1;
	hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &flatPixelShaderBuffer);
	if(FAILED(hr)) return 1;
	ID3D11Buffer *vertexParamsBuffer = nullptr, *pixelParamsBuffer = nullptr, *lightsBuffer = nullptr;
	buffDesc = {sizeof(VertexParams), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
	hr = device->CreateBuffer(&buffDesc, nullptr, &vertexParamsBuffer);
	if(FAILED(hr)) return 1;
	buffDesc.ByteWidth = sizeof(PixelParams);
	hr = device->CreateBuffer(&buffDesc, nullptr, &pixelParamsBuffer);
	if(FAILED(hr)) return 1;
	buffDesc.ByteWidth = sizeof(LightsParams);
	hr = device->CreateBuffer(&buffDesc, nullptr, &lightsBuffer);
	if(FAILED(hr)) return 1;

	TwInit(TW_DIRECT3D11, device);
	TwBar *tweekBar = TwNewBar("Options");
	TwEnumVal blendEnum[] = {{0, "Aditive"}, {1, "Lighten"}, {2, "Lighten Blend"}};
	int32_t blendValue = 1;
	TwAddVarRW(tweekBar, "Light Blend", TwDefineEnum("BlendType", blendEnum, 3), &blendValue, "");
	Vec4 ambientColor = {0.1f, 0.1f, 0.1f, 0.5f};
	TwAddVarRW(tweekBar, "ambientColor", TW_TYPE_COLOR3F, &ambientColor, "group='Ambient' label='Color'");
	TwAddVarRW(tweekBar, "ambientWashoutDamper", TW_TYPE_FLOAT, &ambientColor.a, "group='Ambient' label='Washout Damper' step='0.01'");
	int32_t topLightRingNum = 0, bottomLightRingNum = 0;
	bool topLightRingRotate = true, bottomLightRingRotate = true, topLightRingShow = true, bottomLightRingShow = true;
	float topLightRingRad = 1.5f, bottomLightRingRad = 1.5f, topLightRingHeight = 1.5f, bottomLightRingHeight = -1.5f;
	TwAddVarRW(tweekBar, "TLRN", TW_TYPE_INT32, &topLightRingNum, "group='Top Light Ring' label='Number' min='0' max='8'");
	TwAddVarRW(tweekBar, "BLRN", TW_TYPE_INT32, &bottomLightRingNum, "group='Bottom Light Ring' label='Number' min='0' max='8'");
	TwAddVarRW(tweekBar, "TLRR", TW_TYPE_BOOLCPP, &topLightRingRotate, "group='Top Light Ring' label='Rotate'");
	TwAddVarRW(tweekBar, "BLRR", TW_TYPE_BOOLCPP, &bottomLightRingRotate, "group='Bottom Light Ring' label='Rotate'");
	TwAddVarRW(tweekBar, "TLRS", TW_TYPE_FLOAT, &topLightRingRad, "group='Top Light Ring' label='Radius' step='0.01'");
	TwAddVarRW(tweekBar, "BLRS", TW_TYPE_FLOAT, &bottomLightRingRad, "group='Bottom Light Ring' label='Radius' step='0.01'");
	TwAddVarRW(tweekBar, "TLRH", TW_TYPE_FLOAT, &topLightRingHeight, "group='Top Light Ring' label='Height' step='0.01'");
	TwAddVarRW(tweekBar, "BLRH", TW_TYPE_FLOAT, &bottomLightRingHeight, "group='Bottom Light Ring' label='Height' step='0.01'");
	TwAddVarRW(tweekBar, "TLRD", TW_TYPE_BOOLCPP, &topLightRingShow, "group='Top Light Ring' label='Show Lights'");
	TwAddVarRW(tweekBar, "BLRD", TW_TYPE_BOOLCPP, &bottomLightRingShow, "group='Bottom Light Ring' label='Show Lights'");
	LightsParams lightsParams;
	float intencity[16];
	for(int i=0 ; i<16 ; ++i)
	{
		intencity[i] = 10.0f;
		lightsParams.Color[i] = {1.0f, 1.0f, 1.0f};
	}
	for(int i=0 ; i<8 ; ++i)
	{
		TwAddVarRW(tweekBar, ("LI" + std::to_string(i)).c_str(), TW_TYPE_FLOAT, intencity+i, ("group='Light " + std::to_string(i+1) + 
			"' label='Intencity' min='0' step='.1'").c_str());
		TwAddVarRW(tweekBar, ("LC" + std::to_string(i)).c_str(), TW_TYPE_COLOR3F, lightsParams.Color+i, ("group='Light " + std::to_string(i+1) + 
			"' label='Color'").c_str());
		TwDefine(("'Options'/'Light " + std::to_string(i+1) + "' group='Top Light Ring' opened='false'").c_str());
	}
	for(int i=8 ; i<16 ; ++i)
	{
		TwAddVarRW(tweekBar, ("LI" + std::to_string(i)).c_str(), TW_TYPE_FLOAT, intencity+i, ("group='Light " + std::to_string(i+1) + 
			"' label='Intencity' min='0' step='.1'").c_str());
		TwAddVarRW(tweekBar, ("LC" + std::to_string(i)).c_str(), TW_TYPE_COLOR3F, lightsParams.Color+i, ("group='Light " + std::to_string(i+1) + 
			"' label='Color'").c_str());
		TwDefine(("'Options'/'Light " + std::to_string(i+1) + "' group='Bottom Light Ring' opened='false'").c_str());
	}
	TwDefine("'Options'/'Ambient' opened='false'");
	TwDefine("'Options'/'Bottom Light Ring' opened='false'");

	// loop vars
	bool running = true;
	ID3D11RenderTargetView *renderTargetView = nullptr;
	ID3D11Texture2D *depth = nullptr;
	ID3D11DepthStencilView *depthView = nullptr;
	float clearColor[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
	float topRingLightRotPos = 0.0f, bottomRingLightRotPos = 0.0f;
	VertexParams vertexParams;
	for(int i=0 ; i<16 ; ++i) vertexParams.projMatrix[i] = 0.0f;
	vertexParams.projMatrix[0] = vertexParams.projMatrix[5] = 1.0f;
	vertexParams.projMatrix[10] = vertexParams.projMatrix[14] = -1.0f;
	vertexParams.projMatrix[11] = -0.01f;

	// loop logic
	while(running)
	{
		// window stuff
		MSG msg = {0};
		while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if(msg.message == WM_QUIT) running = false;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if(winresize)
		{
			RECT winRect;
			GetClientRect(winHwnd, &winRect);
			int x = winRect.right - winRect.left;
			int y = winRect.bottom - winRect.top;

			if(renderTargetView) renderTargetView->Release();
			renderTargetView = nullptr;
			if(depth) depth->Release();
			depth = nullptr;
			if(depthView) depthView->Release();
			depthView = nullptr;
			swapChain->ResizeBuffers(0, x, y, DXGI_FORMAT_UNKNOWN, 0);

			ID3D11Texture2D* backBuffer = nullptr;
			hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
			if(FAILED(hr)) return 1;
			device->CreateRenderTargetView(backBuffer, NULL, &renderTargetView);
			backBuffer->Release();
			if(FAILED(hr)) return 1;

			D3D11_TEXTURE2D_DESC depthDesc = {800, 600, 1, 1, DXGI_FORMAT_D24_UNORM_S8_UINT, {1,0}, 
				D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0};
			depthDesc.Width = x; depthDesc.Height = y;
			hr = device->CreateTexture2D(&depthDesc, NULL, &depth);
			if(FAILED(hr)) return 1;

			D3D11_DEPTH_STENCIL_VIEW_DESC stencilDesc;
			ZeroMemory(&stencilDesc, sizeof(stencilDesc));
			stencilDesc.Format = depthDesc.Format;
			stencilDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			stencilDesc.Texture2D.MipSlice = 0;
			hr = device->CreateDepthStencilView(depth, &stencilDesc, &depthView);
			if(FAILED(hr)) return 1;

			immediateContext->OMSetRenderTargets(1, &renderTargetView, depthView);
			D3D11_VIEWPORT viewPort = {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f};
			viewPort.Width = float(x); viewPort.Height = float(y);
			immediateContext->RSSetViewports(1, &viewPort);

			float sy = tan((3*PI)/8);
			vertexParams.projMatrix[0] = sy/(float(x)/y);
			vertexParams.projMatrix[5] = sy;
			winresize = false;
		}

		// draw stuff
		immediateContext->ClearRenderTargetView(renderTargetView, clearColor);
		immediateContext->ClearDepthStencilView(depthView, D3D11_CLEAR_DEPTH, 1.0f, 0);

		for(int i=0 ; i<16 ; ++i) 
		{
			lightsParams.Pos[i] = {-1000.0f, -1000.0f, -1000.0f, 0.3f};
			lightsParams.Intencity_[i].x = intencity[i];
		}
		if(topLightRingRotate) topRingLightRotPos += 0.01f;
		if(bottomLightRingRotate) bottomRingLightRotPos += 0.01f;
		for(int i=0 ; i<topLightRingNum ; ++i)
		{
			lightsParams.Pos[i].x = std::cos(-PI+(2.0f*PI/topLightRingNum)*i+topRingLightRotPos) * topLightRingRad;
			lightsParams.Pos[i].y = topLightRingHeight;
			lightsParams.Pos[i].z = std::sin(-PI+(2.0f*PI/topLightRingNum)*i+topRingLightRotPos) * topLightRingRad;
		}
		for(int i=0 ; i<bottomLightRingNum ; ++i)
		{
			lightsParams.Pos[i+8].x = std::cos(-PI+(2.0f*PI/bottomLightRingNum)*i+(PI/2)+bottomRingLightRotPos) * bottomLightRingRad;
			lightsParams.Pos[i+8].y = bottomLightRingHeight;
			lightsParams.Pos[i+8].z = std::sin(-PI+(2.0f*PI/bottomLightRingNum)*i+(PI/2)+bottomRingLightRotPos) * bottomLightRingRad;
		}

		immediateContext->VSSetShader(vertexShaderBuffer, nullptr, 0);
		switch(blendValue)
		{
		default: case 0: immediateContext->PSSetShader(additivePixelShaderBuffer, nullptr, 0); break;
		case 1: immediateContext->PSSetShader(lightenPixelShaderBuffer, nullptr, 0); break;
		case 2: immediateContext->PSSetShader(lightenBlendPixelShaderBuffer, nullptr, 0); break;
		}
		immediateContext->UpdateSubresource(lightsBuffer, 0, nullptr, &lightsParams, 0, 0);
		immediateContext->PSSetConstantBuffers(0, 1, &lightsBuffer);
		UINT stride = sizeof(float)*6, offset = 0;
		immediateContext->IASetVertexBuffers(0, 1, &cubeBuff, &stride, &offset);
		immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		
		vertexParams.objPos = {0.0f, 0.0f, 0.0f, 1.0f};
		immediateContext->UpdateSubresource(vertexParamsBuffer, 0, nullptr, &vertexParams, 0, 0);
		immediateContext->VSSetConstantBuffers(0, 1, &vertexParamsBuffer);
		PixelParams pixelParams;
		pixelParams.ambientColor = ambientColor;
		immediateContext->UpdateSubresource(pixelParamsBuffer, 0, nullptr, &pixelParams, 0, 0);
		immediateContext->PSSetConstantBuffers(1, 1, &pixelParamsBuffer);
		immediateContext->Draw(24, 0);

		for(int i=0 ; i<16 ; ++i)
		{
			if(i<8 && !topLightRingShow) continue;
			if(i>=8 && !bottomLightRingShow) continue;
			vertexParams.objPos = lightsParams.Pos[i];
			immediateContext->UpdateSubresource(vertexParamsBuffer, 0, nullptr, &vertexParams, 0, 0);
			immediateContext->VSSetConstantBuffers(0, 1, &vertexParamsBuffer);
			immediateContext->PSSetShader(flatPixelShaderBuffer, nullptr, 0);
			pixelParams.ambientColor = lightsParams.Color[i];
			immediateContext->UpdateSubresource(pixelParamsBuffer, 0, nullptr, &pixelParams, 0, 0);
			immediateContext->PSSetConstantBuffers(1, 1, &pixelParamsBuffer);
			immediateContext->Draw(24, 0);
		}

		TwDraw();
		hr = swapChain->Present(1, 0);
		if(FAILED(hr)) return 1;
	}
	return 0;
}
