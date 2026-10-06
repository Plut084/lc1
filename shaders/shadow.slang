import pass_data;
import shadows;

struct ObjectData {
    float4x4 model;
    float4x4 normal_transform;
};

[shader("vertex")]
float4 shadowMain(float3 position : POSITION) : SV_Position
{
    ConstantBuffer<ShadowData> shadow = ResourceDescriptorHeap[pass_data.shadow];
    ConstantBuffer<ObjectData> object_data = ResourceDescriptorHeap[pass_data.object];
    float4 world = object_data.model[0] * position.x + object_data.model[1] * position.y + object_data.model[2] * position.z + object_data.model[3];
    return shadow.view_projection[0] * world.x + shadow.view_projection[1] * world.y + shadow.view_projection[2] * world.z + shadow.view_projection[3] * world.w;
}
