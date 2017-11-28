/*
 * Copyright 2011-2015 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

CCL_NAMESPACE_BEGIN

/* Shadow ray cast for AO. */
ccl_device void shadow_blocked_ao(KernelGlobals *kg)
{
	unsigned int ao_queue_length = kernel_split_params.queue_index[QUEUE_SHADOW_RAY_CAST_AO_RAYS];

	int ray_index = QUEUE_EMPTY_SLOT;
	int thread_index = ccl_global_id(1) * ccl_global_size(0) + ccl_global_id(0);
	if(thread_index < ao_queue_length) {
		ray_index = get_ray_index(kg, thread_index, QUEUE_SHADOW_RAY_CAST_AO_RAYS,
		                          kernel_split_state.queue_data, kernel_split_params.queue_size, 1);
	}

	if(ray_index == QUEUE_EMPTY_SLOT) {
		return;
	}

	ShaderData *sd = kernel_split_sd(sd, ray_index);
	ShaderData *emission_sd = AS_SHADER_DATA(&kernel_split_state.sd_DL_shadow[ray_index]);
	PathRadiance *L = &kernel_split_state.path_radiance[ray_index];
	ccl_global PathState *state = &kernel_split_state.path_state[ray_index];
	float3 throughput = kernel_split_state.throughput[ray_index];

#ifdef __BRANCHED_PATH__
	if(!kernel_data.integrator.branched || IS_FLAG(kernel_split_state.ray_state, ray_index, RAY_BRANCHED_INDIRECT)) {
#endif
		kernel_path_ao(kg, sd, emission_sd, L, state, throughput, shader_bsdf_alpha(kg, sd));
#ifdef __BRANCHED_PATH__
	}
	else {
		kernel_branched_path_ao(kg, sd, emission_sd, L, state, throughput);
	}
#endif
}

/* Shadow ray cast for direct visible light. */
ccl_device void shadow_blocked_dl(KernelGlobals *kg)
{
	unsigned int dl_queue_length = kernel_split_params.queue_index[QUEUE_SHADOW_RAY_CAST_DL_RAYS];

	int ray_index = QUEUE_EMPTY_SLOT;
	int thread_index = ccl_global_id(1) * ccl_global_size(0) + ccl_global_id(0);
	if(thread_index < dl_queue_length) {
		ray_index = get_ray_index(kg, thread_index, QUEUE_SHADOW_RAY_CAST_DL_RAYS,
		                          kernel_split_state.queue_data, kernel_split_params.queue_size, 1);
	}

#ifdef __BRANCHED_PATH__
	/* TODO(mai): move this somewhere else? */
	if(thread_index == 0) {
		/* Clear QUEUE_INACTIVE_RAYS before next kernel. */
		kernel_split_params.queue_index[QUEUE_INACTIVE_RAYS] = 0;
	}
#endif  /* __BRANCHED_PATH__ */

	if(ray_index == QUEUE_EMPTY_SLOT)
		return;

	ccl_global PathState *state = &kernel_split_state.path_state[ray_index];
	PathRadiance *L = &kernel_split_state.path_radiance[ray_index];
	ShaderData *sd = kernel_split_sd(sd, ray_index);
	float3 throughput = kernel_split_state.throughput[ray_index];
	ShaderData *emission_sd = AS_SHADER_DATA(&kernel_split_state.sd_DL_shadow[ray_index]);

#  if defined(__BRANCHED_PATH__) || defined(__SHADOW_TRICKS__)
	bool use_branched = false;
	int all = 0;

	if(state->flag & PATH_RAY_SHADOW_CATCHER) {
		use_branched = true;
		all = 1;
	}
#    if defined(__BRANCHED_PATH__)
	else if(kernel_data.integrator.branched) {
		use_branched = true;

		if(IS_FLAG(kernel_split_state.ray_state, ray_index, RAY_BRANCHED_INDIRECT)) {
			all = (kernel_data.integrator.sample_all_lights_indirect);
		}
		else
		{
			all = (kernel_data.integrator.sample_all_lights_direct);
		}
	}
#    endif  /* __BRANCHED_PATH__ */

	if(use_branched) {
		kernel_branched_path_surface_connect_light(kg,
		                                           sd,
		                                           emission_sd,
		                                           state,
		                                           throughput,
		                                           1.0f,
		                                           L,
		                                           all);
	}
	else
#  endif  /* defined(__BRANCHED_PATH__) || defined(__SHADOW_TRICKS__)*/
	{
		LightSample ls = kernel_split_state.light_sample[ray_index];

		float terminate = path_state_rng_light_termination(kg, state);

		Ray ray;
		ray.time = sd->time;

		BsdfEval L_light;
		bool is_lamp;

		if(direct_emission_finish(kg, sd, emission_sd, &ls, state, &ray, &L_light, &is_lamp, terminate)) {
			/* trace shadow ray */
			float3 shadow;

			if(!shadow_blocked(kg,
				               sd,
				               emission_sd,
				               state,
				               &ray,
				               &shadow))
			{
				/* accumulate */
				path_radiance_accum_light(L, state, throughput, &L_light, shadow, 1.0f, is_lamp);
			}
			else {
				path_radiance_accum_total_light(L, state, throughput, &L_light);
			}
		}
	}
}

ccl_device void kernel_shadow_blocked(KernelGlobals *kg)
{
	shadow_blocked_ao(kg);
	shadow_blocked_dl(kg);
}

CCL_NAMESPACE_END