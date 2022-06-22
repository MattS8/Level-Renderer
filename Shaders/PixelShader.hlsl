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

struct PIXEL_SHADER_DATA
{
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
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

[[vk::binding(0, 0)]]
StructuredBuffer<SHADER_MODEL_DATA> SceneData;

//[[vk::binding(0, 0)]]
//StructuredBuffer<PIXEL_SHADER_DATA> SceneData;

[[vk::binding(0, 1)]]
Texture2D diffuseMap;
[[vk::binding(0, 1)]]
SamplerState qualityFilter;
[[vk::binding(0, 2)]]
Texture2D specularMap;
[[vk::binding(0, 2)]]
SamplerState specQualityFilter;
[[vk::binding(0, 3)]]
Texture2D normalMap;

//Tangentless normal mapping (http://www.thetenthplanet.de) - Magic
float3x3 cotangent_frame(float3 normalVec, float3 pixelVec, float2 uv)
{
    // get edge vec­tors of the pix­el tri­an­gle
    float3 dp1 = ddx(pixelVec);
    float3 dp2 = ddy(pixelVec);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    
    // solve the lin­ear sys­tem
    float3 dp2perp = cross(dp2, normalVec);
    float3 dp1perp = cross(normalVec, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    
    // con­struct a scale-invari­ant frame 
    float invmax = rsqrt(max(dot(T, T), dot(B, B)));
    return float3x3(T * invmax, B * invmax, normalVec);
}


float3 perturb_normal(float3 normalVec, float3 viewVec, float2 uv)
{
    float3 normMap = normalMap.Sample(qualityFilter, uv);
    float MAX_CHANNEL_VAL = 255.0f;
    float HALF_CHANNEL_VAL = 127.0f;
    
    normMap = normMap * MAX_CHANNEL_VAL / HALF_CHANNEL_VAL - (HALF_CHANNEL_VAL + 1) / HALF_CHANNEL_VAL;
    
    // Flip green channel
    normMap.y = normMap.y * -1;
    
    return normalize(cotangent_frame(normalVec, (viewVec * -1), uv) * normMap);

}

float4 main(PS_INPUT psInput) : SV_Target
{
    // Sample diffuse texture pixel
    float4 textureColor = diffuseMap.Sample(qualityFilter, psInput.uvw.xy);
    
    // Sample specular texture pixel
    float4 specularColor = specularMap.Sample(qualityFilter, psInput.uvw.xy);
    
    // Get view direction for normal calcs
    float3 viewDirection = normalize(SceneData[0].cameraPos.xyz - psInput.posW);
  
    // Find perturb normal for tangentless normals
    float3 normal = perturb_normal(normalize(psInput.nrmW), viewDirection, psInput.uvw.xy);
    
    float3 worldNormalized = normalize(psInput.nrmW);
    
    // Directional Lighting
    float directionalLighting = saturate(dot(-normalize(SceneData[0].lightDirection.xyz), worldNormalized));
    
    // Ambient Lighting
    float ambientLighting = saturate(SceneData[0].ambientColor.xyz + directionalLighting);
    
    // Specular
    float3 halfVec = normalize(-normalize(SceneData[0].lightDirection.xyz) + viewDirection);
    float intensity = max(pow(saturate(dot(worldNormalized, halfVec)), SceneData[0].materials[material_offset].Ns), 0);
    float3 reflectedLight = SceneData[0].lightColor.xyz * SceneData[0].materials[material_offset].Ks * intensity * specularColor.xyz;

    float3 diffuseReflectivity = SceneData[0].materials[material_offset].Kd;
    float3 emmisiveReflectivity = SceneData[0].materials[material_offset].Ke;
    
    return float4(textureColor.xyz * diffuseReflectivity * ambientLighting + reflectedLight + emmisiveReflectivity, 1);
}

//float4 main(PS_INPUT psInput) : SV_TARGET
//{
//	//float lightRatio = saturate(dot(-normalize(SceneData[0].lightDirection), psInput.nrmW)));
//	//float3 lightColor;
//	//	lightColor[0] = SceneData[0].ambientColor[0] + lightRatio;
//	//	lightColor[0] = SceneData[0].ambientColor[1] + lightRatio;
//	//	lightColor[0] = SceneData[0].ambientColor[1] + lightRatio;
//	//float3 resultColor = mul(saturate(lightColor), SceneData[0].materials[material_offset].Kd);

	
//	// Directional Lighting
//    float lightAmount = saturate(dot(-normalize(SceneData[0].lightDirection), normalize(psInput.nrmW)));
	
//	// Ambient Lighting
//    float3 fullAmount;
//    fullAmount.x = SceneData[0].ambientColor.x + lightAmount;
//    fullAmount.y = SceneData[0].ambientColor.y + lightAmount;
//    fullAmount.z = SceneData[0].ambientColor.z + lightAmount;
//    fullAmount = saturate(fullAmount);

//	//float3 litColor = mul(SceneData[0].materials[material_offset].Kd, fullAmount);
	
//    float3 litColor = SceneData[0].materials[material_offset].Kd;
//    litColor.x *= fullAmount.x;
//    litColor.y *= fullAmount.y;
//    litColor.z *= fullAmount.z;

//    float3 worldPos = psInput.posW;
//    float3 viewDirection = normalize(SceneData[0].cameraPos - worldPos);
//    float3 halfVec = normalize(-normalize(SceneData[0].lightDirection) + viewDirection);
//    float intensity = max(pow(saturate(dot(normalize(psInput.nrmW), halfVec)), SceneData[0].materials[material_offset].Ns), 0);

//    float3 ambientColor = SceneData[0].ambientColor;
//    float3 reflectedLight = SceneData[0].lightColor * SceneData[0].materials[material_offset].Ks * intensity;
	
//    float3 totalLight = litColor + reflectedLight + SceneData[0].materials[material_offset].Ke;
//    float4 finalColor = float4(totalLight, 1);
//    finalColor *= diffuseMap.Sample(qualityFilter, psInput.uvw);
//    return finalColor;

//}