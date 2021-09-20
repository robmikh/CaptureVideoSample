#include "pch.h"
#include "Direct3D11SceneRenderer.h"
#include "Shaders.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::Graphics;
}

Direct3D11SceneRenderer::Direct3D11SceneRenderer(
    winrt::com_ptr<ID3D11Device> const& d3dDevice, 
    winrt::SizeInt32 const& outputSize)
{
    m_d3dDevice = d3dDevice;
    m_d3dDevice->GetImmediateContext(m_d3dContext.put());

    // Create render target
    CreateAndSetRenderTarget(outputSize);

    // Load shaders
    CreateAndSetShaders();

    // Vertex buffer
    CreateAndSetVertexLayout();
    CreateAndSetVertexBuffers();
    
    // Index buffer
    CreateAndSetIndexBuffer();
    
    // TODO: Sampler
    // TODO: Shader resource view
    // TODO: Viewport
}

// TODO: Render

void Direct3D11SceneRenderer::CreateAndSetRenderTarget(winrt::Windows::Graphics::SizeInt32 const& size)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = size.Width;
    desc.Height = size.Height;
    desc.ArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    winrt::check_hresult(m_d3dDevice->CreateTexture2D(&desc, nullptr, m_outputTexture.put()));
    winrt::check_hresult(m_d3dDevice->CreateRenderTargetView(m_outputTexture.get(), nullptr, m_renderTargetView.put()));
    ID3D11RenderTargetView* renderTargetView = m_renderTargetView.get();
    m_d3dContext->OMSetRenderTargets(1, &renderTargetView, nullptr);
}

void Direct3D11SceneRenderer::CreateAndSetShaders()
{
    winrt::com_ptr<ID3D11VertexShader> vertexShader;
    winrt::check_hresult(m_d3dDevice->CreateVertexShader(g_vertexShader, sizeof(g_vertexShader), nullptr, vertexShader.put()));
    winrt::com_ptr<ID3D11PixelShader> pixelShader;
    winrt::check_hresult(m_d3dDevice->CreatePixelShader(g_pixelShader, sizeof(g_pixelShader), nullptr, pixelShader.put()));
    m_d3dContext->VSSetShader(vertexShader.get(), nullptr, 0);
    m_d3dContext->PSSetShader(pixelShader.get(), nullptr, 0);
}

void Direct3D11SceneRenderer::CreateAndSetVertexLayout()
{
    winrt::com_ptr<ID3D11InputLayout> vertexInputLayout;
    std::array<D3D11_INPUT_ELEMENT_DESC, 2> vertexLayoutDesc =
    { {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    } };
    winrt::check_hresult(m_d3dDevice->CreateInputLayout(vertexLayoutDesc.data(), vertexLayoutDesc.size(), g_vertexShader, sizeof(g_vertexShader), vertexInputLayout.put()));
    m_d3dContext->IASetInputLayout(vertexInputLayout.get());
}

void Direct3D11SceneRenderer::CreateAndSetVertexBuffers()
{
    std::array<Vertex, 4> vertices =
    { {
        { {-1.0f, 1.0f, 0.0f, 1.0f}, { 0.0f, 0.0f } },
        { {1.0f, 1.0f, 0.0f, 1.0f}, { 1.0f, 0.0f } },
        { {1.0f, -1.0f, 0.0f, 1.0f}, { 1.0f, 1.0f } },
        { {-1.0f, -1.0f, 0.0f, 1.0f}, { 0.0f, 1.0f } },
    } };
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(Vertex) * vertices.size();
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    winrt::check_hresult(m_d3dDevice->CreateBuffer(&desc, nullptr, m_vertexBuffer.put()));
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    winrt::check_hresult(m_d3dDevice->CreateBuffer(&desc, nullptr, m_vertexStagingBuffer.put()));
    UpdateVertexBuffer(vertices);
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    auto vertexBuffer = m_vertexBuffer.get();
    m_d3dContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
}

void Direct3D11SceneRenderer::UpdateVertexBuffer(std::array<Vertex, 4> const& vertices)
{
    // Update the staging texture
    {
        // Map the texture
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        winrt::check_hresult(m_d3dContext->Map(m_vertexStagingBuffer.get(), 0, D3D11_MAP_WRITE, 0, &mapped));
        auto unmap = wil::scope_exit([=]()
            {
                m_d3dContext->Unmap(m_vertexStagingBuffer.get(), 0);
            });

        // Write our data
        WINRT_VERIFY(memcpy_s(mapped.pData, mapped.RowPitch, vertices.data(), sizeof(Vertex) * vertices.size()) == 0);
    }

    // Copy to our vertex buffer
    m_d3dContext->CopyResource(m_vertexBuffer.get(), m_vertexStagingBuffer.get());
}

void Direct3D11SceneRenderer::CreateAndSetIndexBuffer()
{
    std::array<uint16_t, 6> indices =
    {{
        0, 1, 2,
        0, 2, 3
    }};
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(uint16_t) * indices.size();
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = indices.data();
    winrt::check_hresult(m_d3dDevice->CreateBuffer(&desc, nullptr, m_indexBuffer.put()));
    m_d3dContext->IASetIndexBuffer(m_indexBuffer.get(), DXGI_FORMAT_R16_UINT, 0);
}
