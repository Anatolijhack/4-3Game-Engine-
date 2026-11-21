#include "Game2.h"
#include <d3d12.h>
#include "d3dx12.h"
#include <d3dcompiler.h>
#include <wrl.h>
#include "Window.h"
#include <stdexcept>


Game2::Game2(UINT width, UINT height, std::wstring name) : IDX(width, height, name), m_frameIndex(0),
m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)), m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
m_rtvDescriptorHeapSize(0)
{
}

void Game2::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

void Game2::OnUpdate()
{
}

void Game2::OnRender()
{
	PopulateCommandList();

	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	m_swapChain->Present(1, 0);

	WaitforPreviousFrame();
}

void Game2::OnDestroy()
{
	WaitforPreviousFrame();
	CloseHandle(m_fenceEvent);
}


//Simple checkboard texture
std::vector<UINT8> Game2::GetTextureData()
{
	const UINT rowPich = TextureWidth * TexturePizelSize;
	const UINT cellPitch = (rowPich >> 4) ? (rowPich >> 4) : TexturePizelSize; // в байтах
	const UINT cellHeight = (TextureHeight >> 4) ? (TextureHeight >> 4) : 1;
	const UINT textureSize = rowPich * TextureHeight;

	std::vector<UINT8> data(textureSize);
	UINT8* pData = &data[0];

	for (UINT index = 0; index < textureSize; index += TexturePizelSize)
	{
		UINT x = index % rowPich;
		UINT y = index / rowPich;
		UINT j = y / cellHeight;
		UINT i = x / cellPitch;
		//Black cell
		if (i % 2 == j % 2)
		{
			//R
			pData[index] = 0x00;
			//G
			pData[index + 1] = 0x00;
			//B
			pData[index + 2] = 0x00;
			//A
			pData[index + 3] = 0xff;
		}
		//White cell
		else
		{
			// R
			pData[index] = 0xff;
			//G
			pData[index + 1] = 0xff;
			//B
			pData[index + 2] = 0xff;
			//A
			pData[index + 3] = 0xff;
		}
	}

	return data;
}

void Game2::LoadPipeline()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> debugControler;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugControler))))
		{
			debugControler->EnableDebugLayer();
			dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
	CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory));

	if (m_useWarpAdapter)
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter;
		factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));
		D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&my_device));
	}
	else
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwarweAdapter;
		GetHardwareAdapter(factory.Get(), &hardwarweAdapter);
		D3D12CreateDevice(hardwarweAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&my_device));
	}
	//Command Queue
	D3D12_COMMAND_QUEUE_DESC queueDescriptor = {};
	queueDescriptor.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDescriptor.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	my_device->CreateCommandQueue(&queueDescriptor, IID_PPV_ARGS(&m_commandQueue));

	//Swap Chain
	DXGI_SWAP_CHAIN_DESC1 swapChainDescriptor = {};
	swapChainDescriptor.BufferCount = FrameCount;
	swapChainDescriptor.Width = m_weight;
	swapChainDescriptor.Height = m_height;
	swapChainDescriptor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDescriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDescriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDescriptor.SampleDesc.Count = 1;

	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
	factory->CreateSwapChainForHwnd(m_commandQueue.Get(), Window::GetHwnd(), &swapChainDescriptor,
		nullptr,
		nullptr,
		&swapChain);

	factory->MakeWindowAssociation(Window::GetHwnd(), DXGI_MWA_NO_ALT_ENTER);
	swapChain.As(&m_swapChain);

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	//DescriptorHeaps
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescriptor = {};
		rtvHeapDescriptor.NumDescriptors = FrameCount;
		rtvHeapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDescriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		my_device->CreateDescriptorHeap(&rtvHeapDescriptor, IID_PPV_ARGS(&m_rtvHeap));

		
		//SRV for the Texture;Shader Resource View
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDescriptor = {};
		srvHeapDescriptor.NumDescriptors = 1;
		srvHeapDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDescriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		my_device->CreateDescriptorHeap(&srvHeapDescriptor, IID_PPV_ARGS(&m_srvHeap));

		m_rtvDescriptorHeapSize = my_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	}
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		for (UINT frame = 0; frame < FrameCount; frame++)
		{
			m_swapChain->GetBuffer(frame, IID_PPV_ARGS(&m_renderTargets[frame]));
			my_device->CreateRenderTargetView(m_renderTargets[frame].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorHeapSize);
		}
	}
	my_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator));
}

void Game2::LoadAssets()
{
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		//Highest version support. IF SUCCES, HIGHEST version  returned will not be greater then this
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if (FAILED(my_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		CD3DX12_DESCRIPTOR_RANGE1 range[1];
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

		CD3DX12_ROOT_PARAMETER1 rootParameters[1];
		rootParameters[0].InitAsDescriptorTable(1, &range[0], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescriptor;
		rootSignatureDescriptor.Init_1_1(_countof(rootParameters),rootParameters,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Microsoft::WRL::ComPtr<ID3DBlob> signature;
		Microsoft::WRL::ComPtr<ID3DBlob> error;
		D3DX12SerializeVersionedRootSignature(&rootSignatureDescriptor, featureData.HighestVersion, &signature, &error);
		my_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));

	}
	//Create the Pipeline state :compiling and loading shaders
	{
		Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
		Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
#if  defined(_DEBUG)
		UINT compilerFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compilerFlags = 0;
#endif
		Microsoft::WRL::ComPtr<ID3DBlob> errors;

		HRESULT hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders2.hlsl").c_str(),
			nullptr, nullptr, "VSMain", "vs_5_0", compilerFlags, 0, &vertexShader, &errors);
		if (FAILED(hr))
		{
			if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());

			std::wstring path = GetAssettsFullPath(L"shaders2.hlsl");
			wchar_t msg[1024];
			swprintf_s(msg, L"VS compile failed\nHRESULT: 0x%08X\nPath: %s", (unsigned)hr, path.c_str());
			MessageBoxW(nullptr, msg, L"Shader error", MB_OK | MB_ICONERROR);
			throw std::runtime_error("VS compile failed");
		}

		errors.Reset();
		hr = D3DCompileFromFile(GetAssettsFullPath(L"shaders2.hlsl").c_str(),
			nullptr, nullptr, "PSMain", "ps_5_0", compilerFlags, 0, &pixelShader, &errors);
		if (FAILED(hr))
		{
			if (errors) OutputDebugStringA((char*)errors->GetBufferPointer());
			throw std::runtime_error("PS compile failed");
		}
		//Vertex Input Layout
		D3D12_INPUT_ELEMENT_DESC inputElementDescription[] =
		{
			{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
			{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		};

		//PSD
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDescriptor = {};
		psoDescriptor.InputLayout = { inputElementDescription, _countof(inputElementDescription) };
		psoDescriptor.pRootSignature = m_rootSignature.Get();
		psoDescriptor.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDescriptor.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDescriptor.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDescriptor.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDescriptor.DepthStencilState.DepthEnable = FALSE;
		psoDescriptor.DepthStencilState.StencilEnable = FALSE;
		psoDescriptor.SampleMask = UINT_MAX;
		psoDescriptor.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDescriptor.NumRenderTargets = 1;
		psoDescriptor.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
		psoDescriptor.SampleDesc.Count = 1;
		my_device->CreateGraphicsPipelineState(&psoDescriptor, IID_PPV_ARGS(&m_pipelineState));


	}

	//Create Command Last
	my_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList));

	//Vertex Buffer
	{
		const int segments = 32;
		const float radius = 0.25f;
		const float pi = 3.14159265f;

		std::vector<Vertex> vertices;

		for (int i = 0; i < segments; ++i)
		{
			float a0 = 2.0f * pi * i / segments;
			float a1 = 2.0f * pi * (i + 1) / segments;

			vertices.push_back({ {0.0f, 0.0f, 0.0f}, {0.5f, 0.5f} });

			vertices.push_back({ {radius * cosf(a1), radius * sinf(a1) * aspecrt_ratio, 0.0f},
								 {0.5f + 0.5f * cosf(a1), 0.5f - 0.5f * sinf(a1)} });

			vertices.push_back({ {radius * cosf(a0), radius * sinf(a0) * aspecrt_ratio, 0.0f},
								 {0.5f + 0.5f * cosf(a0), 0.5f - 0.5f * sinf(a0)} });
		}

		m_vertexCount = (UINT)vertices.size();
		const UINT vertexBufferSize = m_vertexCount * sizeof(Vertex);

		CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC resourceDescriptor = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

		my_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDescriptor,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer));

		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);
		m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
		memcpy(pVertexDataBegin, vertices.data(), vertexBufferSize);
		m_vertexBuffer->Unmap(0, nullptr);

		//Vertex Buffer View
		m_vertexBufferViews.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferViews.StrideInBytes = sizeof(Vertex);
		m_vertexBufferViews.SizeInBytes = vertexBufferSize;
	}
	//Create the texture
	Microsoft::WRL::ComPtr<ID3D12Resource> textureUploadHeap;
	{
		D3D12_RESOURCE_DESC textureDescriptor = {};
		textureDescriptor.MipLevels = 1;
		textureDescriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDescriptor.Width = TextureWidth;
		textureDescriptor.Height = TextureHeight;
		textureDescriptor.Flags = D3D12_RESOURCE_FLAG_NONE;
		textureDescriptor.DepthOrArraySize = 1;
		textureDescriptor.SampleDesc.Count = 1;
		textureDescriptor.SampleDesc.Quality = 0;
		textureDescriptor.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		CD3DX12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		my_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &textureDescriptor, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_texture));

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);
		CD3DX12_RESOURCE_DESC bufferDest = CD3DX12_RESOURCE_DESC::Buffer((uploadBufferSize));
		CD3DX12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

		my_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDest, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,IID_PPV_ARGS(&textureUploadHeap));

		//Copy data to the intermidiate  Upload Heap  and then schedule a copy
		//From  the Upload Heap to the Texture 2d
		std::vector<UINT8> texture = GetTextureData();

		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = texture.data();
		textureData.RowPitch = TextureWidth * TexturePizelSize;
		textureData.SlicePitch = textureData.RowPitch * TextureHeight;

		UpdateSubresources(m_commandList.Get(), m_texture.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);

		CD3DX12_RESOURCE_BARRIER transitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_commandList->ResourceBarrier(1, &transitionBarrier);

		//SRV for the Texture
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescriptor = {};
		srvDescriptor.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDescriptor.Format = textureDescriptor.Format;
		srvDescriptor.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDescriptor.Texture2D.MipLevels = 1;

		my_device->CreateShaderResourceView(m_texture.Get(), &srvDescriptor, m_srvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	m_commandList->Close();
	ID3D12CommandList* ppCommandList[] = { m_commandList.Get() };;
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandList), ppCommandList);

	//Synchronization objects,upload to GPU
	{
		my_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
		m_fenceValue = 1;

		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			throw std::runtime_error("CreateEvent failed");
		}
		WaitforPreviousFrame();
	}
}

void Game2::PopulateCommandList()
{
	m_commandAllocator->Reset();
	m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
	m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
	m_commandList->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());


	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	CD3DX12_RESOURCE_BARRIER transitionBarrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	m_commandList->ResourceBarrier(1, &transitionBarrier1);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorHeapSize);
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	const float bgClearColor[] = { 0.1f,0.1f,0.1f,0.1f }; //R G B A
	m_commandList->ClearRenderTargetView(rtvHandle, bgClearColor, 0, nullptr);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferViews);
	m_commandList->DrawInstanced(m_vertexCount, 1, 0, 0);

	CD3DX12_RESOURCE_BARRIER transitionBarrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
		m_renderTargets[m_frameIndex].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	m_commandList->ResourceBarrier(1, &transitionBarrier2);
	m_commandList->Close();

}

void Game2::WaitforPreviousFrame()
{
	const UINT64 fence = m_fenceValue;
	m_commandQueue->Signal(m_fence.Get(), fence);
	m_fenceValue++;

	if (m_fence->GetCompletedValue() < fence)
	{
		m_fence->SetEventOnCompletion(fence, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

