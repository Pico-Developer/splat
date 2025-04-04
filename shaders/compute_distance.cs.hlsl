/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

/**
 * Required headers:
 * - constants.hlsl
 * - unpacking.hlsl
 *
 * Required shaders constants:
 * - local_to_clip
 * - num_splats
 * - pos_scale_cm
 * - pos_min_cm
 */

Buffer<uint> positions;
RWBuffer<uint> indices;
RWBuffer<uint> distances;

/**
 * Measure the distance to a splat.
 *
 * @param dispatch_thread_id - The x component is 1:1 with the index of the splat
 * that is being measured.
 */
[numthreads(THREAD_GROUP_SIZE_X, 1, 1)] void main(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
  if (dispatch_thread_id.x >= num_splats) {
    return;
  }

  float4 pos_local =
      unpack_pos(positions[dispatch_thread_id.x], pos_scale_cm, pos_min_cm);
  float4 pos_clip = mul(pos_local, local_to_clip);

  bool inside_frustum = !(pos_clip.x < -pos_clip.w || pos_clip.x > pos_clip.w ||
                          pos_clip.y < -pos_clip.w || pos_clip.y > pos_clip.w ||
                          pos_clip.z > pos_clip.w);

  uint index = dispatch_thread_id.x;
  uint distance = inside_frustum
                      ? uint(saturate(pos_clip.z / pos_clip.w) * DISTANCE_SCALE)
                      : DISTANCE_NOT_VISIBLE;

  indices[dispatch_thread_id.x] = index;
  distances[dispatch_thread_id.x] = distance;
}