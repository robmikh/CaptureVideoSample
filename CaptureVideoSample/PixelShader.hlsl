Texture2D Texture : register(t0);
SamplerState Sampler : register(s0);

struct PixelShaderInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
	return Texture.Sample(Sampler, input.texCoord);
}