import shadows;

struct ObjectData {
    float4x4 model;
    float4x4 normal_transform;
};
[[vk::binding(0, 1)]]
ConstantBuffer<ObjectData> object_data;

[shader("vertex")]
float4 shadowMain(float3 position : POSITION) : SV_Position
{
    return mul(shadow.view_projection, mul(object_data.model, float4(position, 1)));
}
