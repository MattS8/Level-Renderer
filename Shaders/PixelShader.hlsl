#define MAX_INSTANCE_PER_DRAW 1024
struct OBJ_ATTRIBUTES
{
    float3 Kd; // diffuse reflectivity
    float d; // dissolve (transparency) 
    float3 Ks; // specular reflectivity
    float Ns; // specular exponent
    float3 Ka; // ambient reflectivity
    float sharpness; // local reflection map sharpness
    float3 Tf; // transmission filter
    float Ni; // optical density (index of refraction)
    float3 Ke; // emissive reflectivity
    int illum; // illumination model
};

struct SHADER_MODEL_DATA
{
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
    float4 cameraPos;
    matrix viewMatrix;
    matrix projectionMatrix;
    matrix matrices[MAX_INSTANCE_PER_DRAW];
    OBJ_ATTRIBUTES materials[MAX_INSTANCE_PER_DRAW];
};

[[vk::push_constant]]
cbuffer MESH_INDEX
{
    uint material_offset;
    uint matrix_offset;
};
// an ultra simple hlsl pixel shader
struct PS_INPUT
{
    float4 posH : SV_POSITION;
    float3 nrmW : NORMAL;
    float3 posW : WORLD;
    float3 uvw : UVW;
};

StructuredBuffer<SHADER_MODEL_DATA> SceneData;

float4 main(PS_INPUT psInput) : SV_TARGET
{
	//float lightRatio = saturate(dot(-normalize(SceneData[0].lightDirection), psInput.nrmW)));
	//float3 lightColor;
	//	lightColor[0] = SceneData[0].ambientColor[0] + lightRatio;
	//	lightColor[0] = SceneData[0].ambientColor[1] + lightRatio;
	//	lightColor[0] = SceneData[0].ambientColor[1] + lightRatio;
	//float3 resultColor = mul(saturate(lightColor), SceneData[0].materials[material_offset].Kd);

	
	// Directional Lighting
    float lightAmount = saturate(dot(-normalize(SceneData[0].lightDirection), normalize(psInput.nrmW)));
	
	// Ambient Lighting
	//float fullAmount = lightAmount;
	//	fullAmount += SceneData[0].ambientColor.x;
	//	fullAmount += SceneData[0].ambientColor.y;
	//	fullAmount += SceneData[0].ambientColor.z;
	//	fullAmount += SceneData[0].ambientColor.w;

	//float fullAmount = saturate(lightAmount + SceneData[0].ambientColor);

    float3 fullAmount;
    fullAmount.x = SceneData[0].ambientColor.x + lightAmount;
    fullAmount.y = SceneData[0].ambientColor.y + lightAmount;
    fullAmount.z = SceneData[0].ambientColor.z + lightAmount;
    fullAmount = saturate(fullAmount);

	//float3 litColor = mul(SceneData[0].materials[material_offset].Kd, fullAmount);
	
    float3 litColor = SceneData[0].materials[material_offset].Kd;
    litColor.x *= fullAmount.x;
    litColor.y *= fullAmount.y;
    litColor.z *= fullAmount.z;

    float3 worldPos = psInput.posW;
    float3 viewDirection = normalize(SceneData[0].cameraPos - worldPos);
    float3 halfVec = normalize(-normalize(SceneData[0].lightDirection) + viewDirection);
    float intensity = max(pow(saturate(dot(normalize(psInput.nrmW), halfVec)), SceneData[0].materials[material_offset].Ns), 0);

    float3 ambientColor = SceneData[0].ambientColor;
    float3 reflectedLight = SceneData[0].lightColor * SceneData[0].materials[material_offset].Ks * intensity;
	
    float3 totalLight = litColor + reflectedLight + SceneData[0].materials[material_offset].Ke;

    return float4(totalLight, 1);
}