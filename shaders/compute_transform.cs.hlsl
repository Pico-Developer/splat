/*
  Copyright (c) 2025 PICO Technology Co., Ltd. See LICENSE.md.
*/

/**
 * Required headers:
 * - constants.hlsl
 * - unpacking.hlsl
 *
 * Required shaders constants:
 * - local_to_view
 * - two_focal_length
 * - num_splats
 * - pos_scale_cm
 * - pos_min_cm
 */

Buffer<uint> positions;
Buffer<uint2> covariances;
RWBuffer<half4> transforms;

/**
 * Calculate a 2x2 transform for projecting a splat into screen space.
 *
 * TODO(seth): Review which operations can drop to float16.
 * The math involved in calculating the transform is particularly sensitive to
 * precision. As such, I've left it all in float32 for now.
 *
 * @param dispatch_thread_id - The x component is 1:1 with the index of the splat
 * that is transformed.
 */
[numthreads(THREAD_GROUP_SIZE_X, 1, 1)] void main(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
  uint splat_id = dispatch_thread_id.x;

  if (splat_id >= num_splats) {
    return;
  }

  float4 pos_local = unpack_pos(positions[splat_id], pos_scale_cm, pos_min_cm);
  float3 pos_view = mul(pos_local, local_to_view);

  /**
	 * Calculate Σ', an approximation of the projected covariance matrix:
	 *
	 * Σ' = J * W * Σ * W^T * J^T
	 *
	 * Where:
	 *   Σ: 3x3 covariance matrix (https://en.wikipedia.org/wiki/Covariance_matrix).
	 *   J: Affine approximation of projection.
	 *   W: View matrix.
	 *   Σ': Projected 2x2 covariance matrix.
	 *
	 * From Zwicker et al.'s *EWA Splatting*.
	 */

  /**
	 * Assemble covariance matrix Σ.
	 * The diagonal represents the variance along each axis (x, y, z).
	 * The off-diagonal is the covariance between axes (xy, xz, yz).
	 */
  float3x3 sig = unpack_cov_mat(covariances[splat_id]);

  /* View matrix W, with model transform multiplied in: */
  float3x3 W = float3x3(local_to_view[0].xyz, local_to_view[1].xyz,
                        local_to_view[2].xyz);

  /**
	 * Calculate J * W:
	 *
	 * Jacobian J approximating projection at point t:
	 *
	 * J = [ 1/t_2   0   -t_0/t_2^2 ]
	 *     [  0    1/t_2 -t_1/t_2^2 ]
	 *   = [  1      0   -t_0/t_2   ]
	 *     [  0      1   -t_1/t_2   ] / t_2
	 *
	 * Where t is the position in "camera space", with projection the projection
	 * plane at t_2 = 1.
	 * Note:
	 * 1. As our focal length is *not* necessarily (or likely) 1, we need to
	 * scale J up by our focal length to scale the final splats correctly. This
	 * is applied at the end of the shader, as it requires only one multipy op
	 * (being after sqrt(λ)), versus two here.
	 * 2. Likewise, t_2 is multiplied in at the end as well, for the same reason.
	 *
	 * From Zwicker et al.'s *EWA Splatting*.
	 *
	 * From here, we can reduce the calculation of J * W to:
	 *
	 * J * W = [ W_00 - t_0/t_2 * W_20   W_01 - t_0/t_2 * W_21   W_02 - t_0/t_2 * W_22 ]
	 *         [ W_10 - t_0/t_2 * W_20   W_11 - t_0/t_2 * W_21   W_12 - t_0/t_2 * W_22 ] / t_2
	 *
	 * Note: W is transposed below due to Unreal/Direct3D's customary order of
	 * operations and matrix layout (i.e. v * M^T instead of M * v).
	 */
  float scale_x = -pos_view.x / pos_view.z;
  float scale_y = -pos_view.y / pos_view.z;

  /**
	 * Calculate Σ':
	 *
	 * Σ' =  J * W * Σ  *  W^T * J^T
	 *    = (J * W * Σ) * (J   * W)^T
	 *
	 * Note: J here refers to J * t_0, as that factor is multiplied in later.
	 */

  /* Calculate (J * W). */

  /* First row: (J * W)_0. */
  float3 jw_0 = float3(W[0][0] + scale_x * W[0][2], W[1][0] + scale_x * W[1][2],
                       W[2][0] + scale_x * W[2][2]);

  /* Second row: (J * W)_1. */
  float3 jw_1 = float3(W[0][1] + scale_y * W[0][2], W[1][1] + scale_y * W[1][2],
                       W[2][1] + scale_y * W[2][2]);

  /* Calculate (J * W * Σ). */

  /* First row: (J * W * Σ)_0. */
  float3 jw_sig_0 = mul(jw_0, sig);

  /* Second row: (J * W * Σ)_1. */
  float3 jw_sig_1 = mul(jw_1, sig);

  /* Calculate Σ'. */

  /* Σ'_00 = (J * W * Σ)_0 * ((J * W)_0)^T */
  float sig_p_00 = mul(jw_sig_0, jw_0);

  /**
	 * Σ'_01 = (J * W * Σ)_0 * ((J * W)_1)^T
	 * Σ'_10 = Σ'_01, as Σ' is symmetric.
	 */
  float sig_p_01_10 = mul(jw_sig_0, jw_1);

  /* Σ'_11 = (J * W * Σ)_1 * ((J * W)_1)^T */
  float sig_p_11 = mul(jw_sig_1, jw_1);

  /**
	 * Given the characteristic polynomial p of Σ':
	 *
	 * p_Σ'(λ) = λ^2 - (Σ'_00 + Σ'_11) * λ + (Σ'_00 * Σ'_11 - Σ'_01 * Σ'_10)
	 * p_Σ'(λ) = λ^2 -      tr(Σ')     * λ +             det(Σ')
	 *
	 * Calculate the eigenvalues λ with the quadratic formula:
	 *
	 * λ = (tr(Σ')     ± sqrt( tr(Σ')^2      - 4 * det(Σ'))) / 2
	 *   =  tr(Σ') / 2 ± sqrt( tr(Σ')^2      - 4 * det(Σ')) / 2
	 *   =  tr(Σ') / 2 ± sqrt((tr(Σ') / 2)^2 -     det(Σ'))
	 *
	 * Calculate the determinant of Σ':
	 *
	 * det(Σ') = Σ'_00 * Σ'_11 - Σ'_01 * Σ'_10
	 */
  float det = sig_p_00 * sig_p_11 - sig_p_01_10 * sig_p_01_10;

  /**
	 * Calculate trace of Σ':
	 *
	 * tr(Σ') = Σ'_00 + Σ'_11
	 */
  float trace = sig_p_00 + sig_p_11;

  /**
	 * Calculate the square root of the discriminant:
	 *
	 * sqrt(Δ(p_Σ'(λ))) = sqrt(tr(Σ')^2 - 4 * det(Σ'))
	 */
  float sqrt_disc = sqrt(trace * trace - 4 * det);

  /**
	 * Calculate two times the eigenvalues λ:
	 *
	 * 2 * λ = tr(Σ') ± sqrt(tr(Σ')^2 - 4 * det(Σ'))
	 */
  float2 two_lmb = float2(trace + sqrt_disc, trace - sqrt_disc);

  /**
	 * Calculate the square root of two times the standard deviations σ:
	 *
	 * sqrt(2) * σ = sqrt(2 * λ)
	 */
  float2 sqrt_two_sigma = sqrt(two_lmb);

  /**
	 * Calculate the first eigenvector v_0:
	 *
	 * v_0 = k [    b    ]
	 *         [ λ_0 - a ]
	 *
	 * Where k is non-zero.
	 * Normalize to make unit for scaling.
	 */
  float2 v_0 = normalize(float2(sig_p_01_10, two_lmb.x / 2.f - sig_p_00));

  /**
	 * scale the transform, such that the subsequent vertex shaders outputs a
	 * quad of radius σ.
	 *
	 * Note:
	 * 1. As the affine approximation of the projection above (J) assumes a
	 * focal length of 1, we need to multiply in the actual focal length to
	 * scale the splats correctly. Doing it here requires one op, versus two
	 * if done before sqrt(λ).
	 * 2. t_0 is divided out here, to save a multiplication.
	 * 3. Using float4 because Unreal was unhappy with float2x2 in UAV.
	 * 4. The 2 multiplied into the focal length is not related; it is to save
	 * an extra multiplication in the VS. This 2 is part of the screen size
	 * scaling math, where the splat is scaled by (2 / screen size) to scale
	 * from [0, w/h) of screen space to [-1, 1] of NDC.
	 */
  float2 scale = two_focal_length / pos_view.z * sqrt_two_sigma;
  transforms[splat_id] = half4(v_0.x, -v_0.y, v_0.y, v_0.x) * scale.xyxy;
}