#pragma once

class Direct3D11SceneRenderer
{
public:
    Direct3D11SceneRenderer(
        winrt::com_ptr<ID3D11Device> const& d3dDevice,
        winrt::Windows::Graphics::SizeInt32 const& outputSize);

    winrt::com_ptr<ID3D11Texture2D> const& OutputTexture() { return m_outputTexture; }

private:
    struct Vertex
    {
        winrt::Windows::Foundation::Numerics::float4 Position;
        winrt::Windows::Foundation::Numerics::float2 TexCoord;
    };

    void CreateAndSetRenderTarget(winrt::Windows::Graphics::SizeInt32 const& size);
    void CreateAndSetShaders();
    void CreateAndSetVertexLayout();
    void CreateAndSetVertexBuffers();
    void UpdateVertexBuffer(std::array<Vertex, 4> const& vertices);
    void CreateAndSetIndexBuffer();

private:
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
    winrt::com_ptr<ID3D11Texture2D> m_outputTexture;
    winrt::com_ptr<ID3D11RenderTargetView> m_renderTargetView;
    winrt::com_ptr<ID3D11Buffer> m_vertexBuffer;
    winrt::com_ptr<ID3D11Buffer> m_vertexStagingBuffer;
    winrt::com_ptr<ID3D11Buffer> m_indexBuffer;
};