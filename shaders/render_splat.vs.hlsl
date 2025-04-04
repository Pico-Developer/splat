/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

/**
 * Required defines:
 * - WITH_VIEW_ID
 *
 * Required shaders constants:
 * - local_to_world
 * - pos_scale_cm
 * - pos_min_cm
 *
 * Required functions:
 * - world_to_clip: half3 -> half4
 * - get_render_resolution: () -> half2
 */

#ifdef GPU_SORT
Buffer<uint> indices;
#else
Buffer<uint2> indices;
#endif
Buffer<uint> positions;
Buffer<half4> transforms;
Buffer<half4> colors;

/**
 * Generates a vertex bounding a splat.
 *
 * @param in_id - Vertex index, of 6 * the number of splats.
 * @param in_view_id - Eye index (if relevant).
 * @param out_position - Clip space position, where (x, y) bounds the splat at
 * chosen value of σ.
 * @param out_sig_div_sqrt_2 - the radius of the splat, in σ's, over sqrt(2).
 * @param out_color - Base color of the splat.
 */
void main(in uint in_id : SV_VertexID,
#if WITH_VIEW_ID
          in uint in_view_id : SV_ViewID,
#endif
          out half2 out_sig_div_sqrt_2 : DELTA_STD_DEVS,
          out nointerpolation half4 out_color : COLOR,
          out half4 out_position : SV_Position) {
  // If using CPU sorting, indices are packed as (index, distance). No #if
  // needed if explicitly adding .x.
  uint splat_id = indices[in_id / 6].x;

  half3 pos_local = unpack_pos(positions[splat_id], pos_scale_cm, pos_min_cm);
  half3 pos_world = mul(pos_local, (half3x3)local_to_world);

  half4 pos_clip = world_to_clip(pos_world
#if WITH_VIEW_ID
                                 ,
                                 in_view_id
#endif
  );

  bool outside_frustum = pos_clip.x < -pos_clip.w || pos_clip.x > pos_clip.w ||
                         pos_clip.y < -pos_clip.w || pos_clip.y > pos_clip.w ||
                         pos_clip.z > pos_clip.w;

  if (outside_frustum) {
    out_position = half4(0.f, 0.f, 0.f, 0.f);
    return;
  }

  /**
	 * Fit triangle to splat.
	 *
	 * 1. Each transform t is of radius 1σ. Scale to the desired number of
	 * standard deviations σ.
	 * 2. transforms are sized in pixels, * 2. Dividing by the resolution of the
	 * render target, and multiplying the dynamic resolution factor, converts
	 * the space to NDC. the scalar 2 is from NDC being of height/width 2, but
	 * is multiplied in in the vertex shader to save an op.
	 * 3. (X, Y) offset is multiplied by W, to cancel out the subsequent
	 * perspective divide (by W). Added to center (X, Y), this gives a
	 * specific vertex of the triangle.
	 * 4. Per vertex, out_sig_div_sqrt_2 is +- xσ/sqrt(2). this is interpolated by
	 * the fragment shader, and used to determine the distance in σ from the
	 * splat's center that a fragment is at.
	 *
	 * Note: Using RADIUS_SIGMA_OVER_SQRT_2 here rather than just +/-1, as it
	 * removes a multiply from the below `out_sig_div_sqrt_2` assignment.
	 */
  half4 t = transforms[splat_id];
  half2 corners[6] = {
      half2(-RADIUS_SIGMA_OVER_SQRT_2, -RADIUS_SIGMA_OVER_SQRT_2),
      half2(RADIUS_SIGMA_OVER_SQRT_2, -RADIUS_SIGMA_OVER_SQRT_2),
      half2(-RADIUS_SIGMA_OVER_SQRT_2, RADIUS_SIGMA_OVER_SQRT_2),
      half2(RADIUS_SIGMA_OVER_SQRT_2, -RADIUS_SIGMA_OVER_SQRT_2),
      half2(-RADIUS_SIGMA_OVER_SQRT_2, RADIUS_SIGMA_OVER_SQRT_2),
      half2(RADIUS_SIGMA_OVER_SQRT_2, RADIUS_SIGMA_OVER_SQRT_2)};
  // Scale to xσ/sqrt(2).
  half2 offset = mul(half2x2(t.x, t.y, t.z, t.w), corners[in_id % 6]);
  // (Pixel size * 2) to NDC.
  offset /= get_render_resolution();

  // Actual vertex position.
  out_position =
      half4(pos_clip.xy + offset * pos_clip.w, pos_clip.z, pos_clip.w);
  // Distance from center, in σ, for interpolating in fragment shader.
  out_sig_div_sqrt_2 = corners[in_id % 6];
  // Color.
  out_color = colors[splat_id];
}