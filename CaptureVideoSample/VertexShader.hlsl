struct VertexShaderInput
{
	float4 position : POSITION;
	float2 texCoord : TEXCOORD0;
};

struct PixelShaderInput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

PixelShaderInput main(VertexShaderInput input)
{
	PixelShaderInput result;
	result.position = input.position;
	result.texCoord = input.texCoord;
	return result;
}