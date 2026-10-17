struct Varying { float4 position : SV_Position; float4 color : COLOR; };
Varying VS(uint id : SV_VertexID) {
    Varying v;
    float2 positions[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    v.position = float4(positions[id], 0.5, 1);
    v.color = float4(0.25, 0.5, 0.75, 1);
    return v;
}
Varying InputVS(float2 position : SHADER_INPUT0, float4 color : SHADER_INPUT1) {
    Varying v; v.position = float4(position, 0.5, 1); v.color = color; return v;
}
float4 PS(Varying v) : SV_Target0 { return v.color; }
struct Targets { float4 a : SV_Target0; float4 b : SV_Target1; };
Targets MRT(Varying v) { Targets t; t.a = float4(1,0,0,1); t.b = float4(0,1,0,1); return t; }
Texture2D<float4> source : register(t0, space0);
SamplerState sourceSampler : register(s0, space0);
float4 SamplePS(Varying v) : SV_Target0 { return source.SampleLevel(sourceSampler, float2(0.5,0.5), 0); }

cbuffer Material : register(b1, space0) { float4 materialColor; };
cbuffer PushData : register(b0, space2147483647) { float4 pushedColor; uint selected; float3 pushPadding; };
float4 UniformPS(Varying v) : SV_Target0 { return materialColor; }
float4 PushPS(Varying v) : SV_Target0 { return pushedColor + float4(float(selected),0,0,0); }
Varying PushVS(uint id : SV_VertexID) { Varying v=VS(id);v.color=pushedColor;return v; }
float4 StagePS(Varying v) : SV_Target0 { return float4(v.color.r,pushedColor.g,0,1); }
ByteAddressBuffer inputBytes : register(t2, space0);
RWByteAddressBuffer outputBytes : register(u3, space0);
StructuredBuffer<float4> inputValues : register(t4, space0);
RWStructuredBuffer<float4> outputValues : register(u5, space0);
RWTexture2D<float4> storageImage : register(u6, space0);
[numthreads(1,1,1)] void RawCS(uint3 id : SV_DispatchThreadID) { outputBytes.Store4(0, inputBytes.Load4(0)); }
[numthreads(1,1,1)] void StructuredCS(uint3 id : SV_DispatchThreadID) { outputValues[0] = inputValues[0]; }
[numthreads(1,1,1)] void PushCS(uint3 id : SV_DispatchThreadID) { outputValues[0] = materialColor + pushedColor; }
[numthreads(1,1,1)] void ImageCS(uint3 id : SV_DispatchThreadID) { storageImage[id.xy] = float4(0,1,0,1); }
cbuffer ArrayInfo : register(b0, space1) { uint chosenTexture; };
Texture2D<float4> textures[] : register(t1, space1);
float4 ArrayPS(Varying v) : SV_Target0 { return textures[NonUniformResourceIndex(chosenTexture)].SampleLevel(sourceSampler,float2(.5,.5),0); }


struct MeshPayload { uint groupCount; };
groupshared MeshPayload meshPayload;
[numthreads(1, 1, 1)]
void TaskAS() {
  meshPayload.groupCount = 1;
  DispatchMesh(meshPayload.groupCount, 1, 1, meshPayload);
}

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void TriangleMS(out vertices Varying vertices[3],
                out indices uint3 triangles[1]) {
  SetMeshOutputCounts(3, 1);
  vertices[0] = VS(0);
  vertices[1] = VS(1);
  vertices[2] = VS(2);
  triangles[0] = uint3(0, 1, 2);
}

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void PayloadMS(in payload MeshPayload payload,
               out vertices Varying vertices[3],
               out indices uint3 triangles[1]) {
  SetMeshOutputCounts(3, payload.groupCount);
  vertices[0] = VS(0);
  vertices[1] = VS(1);
  vertices[2] = VS(2);
  triangles[0] = uint3(0, 1, 2);
}


[numthreads(1, 1, 1)]
void MeshArgumentsCS() {
  outputBytes.Store3(4, uint3(1, 1, 1));
  outputBytes.Store3(24, uint3(1, 1, 1));
}


Texture2D<float> scalarSource : register(t7, space0);
RWTexture2D<float> scalarDestination : register(u8, space0);
[numthreads(1, 1, 1)]
void ScalarCopyCS(uint3 id : SV_DispatchThreadID) {
  scalarDestination[id.xy] = scalarSource.Load(int3(id.xy, 0));
}


Texture2D<float4> stressTextures[] : register(t0, space0);
SamplerState stressSamplers[] : register(s1, space0);
float4 DescriptorReusePS(Varying input) : SV_Target0 {
  uint samplerIndex = input.position.x < 8 ? 0 : 1;
  return stressTextures[NonUniformResourceIndex(2049)].Sample(
      stressSamplers[NonUniformResourceIndex(samplerIndex)], float2(1.25, 0.5));
}


cbuffer LargePushData : register(b0, space2147483647) {
  uint outputIndex;
  float retainedValue;
  float2 largePadding;
  float4x4 firstMatrix;
  float4x4 secondMatrix;
  float4x4 thirdMatrix;
  float4x4 fourthMatrix;
};
[numthreads(1, 1, 1)]
void LargePushCS() {
  outputValues[outputIndex] = float4(float(outputIndex), retainedValue,
                                    fourthMatrix[3][3], 1);
}
cbuffer ArrayPushData : register(b0, space2147483647) {
  float4 arrayValues[2];
};
[numthreads(1, 1, 1)]
void ArrayPushCS() { outputValues[0] = arrayValues[1]; }

cbuffer WideStagePushData : register(b0, space2147483647) {
  float4 stageColor;
  float4x4 firstStageMatrix;
  float4x4 secondStageMatrix;
  float4 stagePadding;
};
Varying WidePushVS(uint id : SV_VertexID) {
  Varying vertex = VS(id);
  vertex.color = stageColor;
  return vertex;
}
float4 WideStagePS(Varying vertex) : SV_Target0 {
  return float4(vertex.color.r, stageColor.g, 0, 1);
}
