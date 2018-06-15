/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2017, Blender Foundation
 * This is a new part of Blender
 *
 * Contributor(s): Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/gpencil_modifier.c
 *  \ingroup bke
 */

 
#include <stdio.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"
#include "BLI_math_vector.h"

#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_gpencil_modifier_types.h"
#include "DNA_modifier_types.h"
#include "DNA_vec_types.h"

#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_lattice.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_object.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

/* *************************************************** */
/* Geometry Utilities */

/* calculate stroke normal using some points */
void BKE_gpencil_stroke_normal(const bGPDstroke *gps, float r_normal[3])
{
	if (gps->totpoints < 3) {
		zero_v3(r_normal);
		return;
	}

	bGPDspoint *points = gps->points;
	int totpoints = gps->totpoints;

	const bGPDspoint *pt0 = &points[0];
	const bGPDspoint *pt1 = &points[1];
	const bGPDspoint *pt3 = &points[(int)(totpoints * 0.75)];

	float vec1[3];
	float vec2[3];

	/* initial vector (p0 -> p1) */
	sub_v3_v3v3(vec1, &pt1->x, &pt0->x);

	/* point vector at 3/4 */
	sub_v3_v3v3(vec2, &pt3->x, &pt0->x);

	/* vector orthogonal to polygon plane */
	cross_v3_v3v3(r_normal, vec1, vec2);

	/* Normalize vector */
	normalize_v3(r_normal);
}

/* Get points of stroke always flat to view not affected by camera view or view position */
static void gpencil_stroke_project_2d(const bGPDspoint *points, int totpoints, vec2f *points2d)
{
	const bGPDspoint *pt0 = &points[0];
	const bGPDspoint *pt1 = &points[1];
	const bGPDspoint *pt3 = &points[(int)(totpoints * 0.75)];

	float locx[3];
	float locy[3];
	float loc3[3];
	float normal[3];

	/* local X axis (p0 -> p1) */
	sub_v3_v3v3(locx, &pt1->x, &pt0->x);

	/* point vector at 3/4 */
	sub_v3_v3v3(loc3, &pt3->x, &pt0->x);

	/* vector orthogonal to polygon plane */
	cross_v3_v3v3(normal, locx, loc3);

	/* local Y axis (cross to normal/x axis) */
	cross_v3_v3v3(locy, normal, locx);

	/* Normalize vectors */
	normalize_v3(locx);
	normalize_v3(locy);

	/* Get all points in local space */
	for (int i = 0; i < totpoints; i++) {
		const bGPDspoint *pt = &points[i];
		float loc[3];

		/* Get local space using first point as origin */
		sub_v3_v3v3(loc, &pt->x, &pt0->x);

		vec2f *point = &points2d[i];
		point->x = dot_v3v3(loc, locx);
		point->y = dot_v3v3(loc, locy);
	}

}

/* Stroke Simplify ------------------------------------- */

/* Reduce a series of points to a simplified version, but
 * maintains the general shape of the series
 *
 * Ramer - Douglas - Peucker algorithm
 * by http ://en.wikipedia.org/wiki/Ramer-Douglas-Peucker_algorithm
 */
static void gpencil_rdp_stroke(bGPDstroke *gps, vec2f *points2d, float epsilon)
{
	vec2f *old_points2d = points2d;
	int totpoints = gps->totpoints;
	char *marked = NULL;
	char work;

	int start = 1;
	int end = gps->totpoints - 2;

	marked = MEM_callocN(totpoints, "GP marked array");
	marked[start] = 1;
	marked[end] = 1;

	work = 1;
	int totmarked = 0;
	/* while still reducing */
	while (work) {
		int ls, le;
		work = 0;

		ls = start;
		le = start + 1;

		/* while not over interval */
		while (ls < end) {
			int max_i = 0;
			float v1[2];
			/* divided to get more control */
			float max_dist = epsilon / 10.0f;

			/* find the next marked point */
			while (marked[le] == 0) {
				le++;
			}

			/* perpendicular vector to ls-le */
			v1[1] = old_points2d[le].x - old_points2d[ls].x;
			v1[0] = old_points2d[ls].y - old_points2d[le].y;

			for (int i = ls + 1; i < le; i++) {
				float mul;
				float dist;
				float v2[2];

				v2[0] = old_points2d[i].x - old_points2d[ls].x;
				v2[1] = old_points2d[i].y - old_points2d[ls].y;

				if (v2[0] == 0 && v2[1] == 0) {
					continue;
				}

				mul = (float)(v1[0] * v2[0] + v1[1] * v2[1]) / (float)(v2[0] * v2[0] + v2[1] * v2[1]);

				dist = mul * mul * (v2[0] * v2[0] + v2[1] * v2[1]);

				if (dist > max_dist) {
					max_dist = dist;
					max_i = i;
				}
			}

			if (max_i != 0) {
				work = 1;
				marked[max_i] = 1;
				totmarked++;
			}

			ls = le;
			le = ls + 1;
		}
	}

	/* adding points marked */
	bGPDspoint *old_points = MEM_dupallocN(gps->points);
	MDeformVert *old_dvert = MEM_dupallocN(gps->dvert);

	/* resize gps */
	gps->flag |= GP_STROKE_RECALC_CACHES;
	gps->tot_triangles = 0;

	int j = 0;
	for (int i = 0; i < totpoints; i++) {
		bGPDspoint *pt_src = &old_points[i];
		bGPDspoint *pt = &gps->points[j];

		MDeformVert *dvert_src = &old_dvert[i];
		MDeformVert *dvert = &gps->dvert[j];

		if ((marked[i]) || (i == 0) || (i == totpoints - 1)) {
			memcpy(pt, pt_src, sizeof(bGPDspoint));
			memcpy(dvert, dvert_src, sizeof(MDeformVert));
			j++;
		}
		else {
			BKE_gpencil_free_point_weights(dvert_src);
		}
	}

	gps->totpoints = j;

	MEM_SAFE_FREE(old_points);
	MEM_SAFE_FREE(old_dvert);
	MEM_SAFE_FREE(marked);
}

/* Simplify stroke using Ramer-Douglas-Peucker algorithm */
void BKE_gpencil_simplify_stroke(bGPDstroke *gps, float factor)
{
	/* first create temp data and convert points to 2D */
	vec2f *points2d = MEM_mallocN(sizeof(vec2f) * gps->totpoints, "GP Stroke temp 2d points");

	gpencil_stroke_project_2d(gps->points, gps->totpoints, points2d);

	gpencil_rdp_stroke(gps, points2d, factor);

	MEM_SAFE_FREE(points2d);
}

/* Simplify alternate vertex of stroke except extrems */
void BKE_gpencil_simplify_fixed(bGPDstroke *gps)
{
	if (gps->totpoints < 5) {
		return;
	}

	/* save points */
	bGPDspoint *old_points = MEM_dupallocN(gps->points);
	MDeformVert *old_dvert = MEM_dupallocN(gps->dvert);

	/* resize gps */
	int newtot = (gps->totpoints - 2) / 2;
	if (((gps->totpoints - 2) % 2) > 0) {
		newtot++;
	}
	newtot += 2;

	gps->points = MEM_recallocN(gps->points, sizeof(*gps->points) * newtot);
	gps->dvert = MEM_recallocN(gps->dvert, sizeof(*gps->dvert) * newtot);
	gps->flag |= GP_STROKE_RECALC_CACHES;
	gps->tot_triangles = 0;

	int j = 0;
	for (int i = 0; i < gps->totpoints; i++) {
		bGPDspoint *pt_src = &old_points[i];
		bGPDspoint *pt = &gps->points[j];

		MDeformVert *dvert_src = &old_dvert[i];
		MDeformVert *dvert = &gps->dvert[j];

		if ((i == 0) || (i == gps->totpoints - 1) || ((i % 2) > 0.0)) {
			memcpy(pt, pt_src, sizeof(bGPDspoint));
			memcpy(dvert, dvert_src, sizeof(MDeformVert));
			j++;
		}
		else {
			BKE_gpencil_free_point_weights(dvert_src);
		}
	}

	gps->totpoints = j;

	MEM_SAFE_FREE(old_points);
	MEM_SAFE_FREE(old_dvert);
}

/* *************************************************** */
/* Modifier Utilities */

/* Lattice Modifier ---------------------------------- */
/* Usually, evaluation of the lattice modifier is self-contained.
 * However, since GP's modifiers operate on a per-stroke basis,
 * we need to these two extra functions that called before/after
 * each loop over all the geometry being evaluated.
 */

/* init lattice deform data */
void BKE_gpencil_lattice_init(Object *ob)
{
	GreasePencilModifierData *md;
	for (md = ob->modifiers.first; md; md = md->next) {
		if (md->type == eGreasePencilModifierType_Lattice) {
			LatticeGreasePencilModifierData *mmd = (LatticeGreasePencilModifierData *)md;
			Object *latob = NULL;

			latob = mmd->object;
			if ((!latob) || (latob->type != OB_LATTICE)) {
				return;
			}
			if (mmd->cache_data) {
				end_latt_deform((struct LatticeDeformData *)mmd->cache_data);
			}

			/* init deform data */
			mmd->cache_data = (struct LatticeDeformData *)init_latt_deform(latob, ob);
		}
	}
}

/* clear lattice deform data */
void BKE_gpencil_lattice_clear(Object *ob)
{
	GreasePencilModifierData *md;
	for (md = ob->modifiers.first; md; md = md->next) {
		if (md->type == eGreasePencilModifierType_Lattice) {
			LatticeGreasePencilModifierData *mmd = (LatticeGreasePencilModifierData *)md;
			if ((mmd) && (mmd->cache_data)) {
				end_latt_deform((struct LatticeDeformData *)mmd->cache_data);
				mmd->cache_data = NULL;
			}
		}
	}
}

/* *************************************************** */
/* Modifier Methods - Evaluation Loops, etc. */

/* check if exist geometry modifiers */
bool BKE_gpencil_has_geometry_modifiers(Object *ob)
{
	GreasePencilModifierData *md;
	for (md = ob->modifiers.first; md; md = md->next) {
		const GreasePencilModifierTypeInfo *mti = BKE_gpencil_modifierType_getInfo(md->type);
		
		if (mti && mti->gp_generateStrokes) {
			return true;
		}
	}
	return false;
}

/* apply stroke modifiers */
void BKE_gpencil_stroke_modifiers(Depsgraph *depsgraph, Object *ob, bGPDlayer *gpl, bGPDframe *UNUSED(gpf), bGPDstroke *gps, bool is_render)
{
	GreasePencilModifierData *md;
	bGPdata *gpd = ob->data;
	const bool is_edit = GPENCIL_ANY_EDIT_MODE(gpd);
	
	for (md = ob->modifiers.first; md; md = md->next) {
		if (GPENCIL_MODIFIER_ACTIVE(md, is_render))
		{
			const GreasePencilModifierTypeInfo *mti = BKE_gpencil_modifierType_getInfo(md->type);
			
			if (GPENCIL_MODIFIER_EDIT(md, is_edit)) {
				continue;
			}
			
			if (mti && mti->gp_deformStroke) {
				mti->gp_deformStroke(md, depsgraph, ob, gpl, gps);
			}
		}
	}
}

/* apply stroke geometry modifiers */
void BKE_gpencil_geometry_modifiers(Depsgraph *depsgraph, Object *ob, bGPDlayer *gpl, bGPDframe *gpf, bool is_render)
{
	GreasePencilModifierData *md;
	bGPdata *gpd = ob->data;
	const bool is_edit = GPENCIL_ANY_EDIT_MODE(gpd);

	for (md = ob->modifiers.first; md; md = md->next) {
		if (GPENCIL_MODIFIER_ACTIVE(md, is_render))
		{
			const GreasePencilModifierTypeInfo *mti = BKE_gpencil_modifierType_getInfo(md->type);
			
			if (GPENCIL_MODIFIER_EDIT(md, is_edit)) {
				continue;
			}

			if (mti->gp_generateStrokes) {
				mti->gp_generateStrokes(md, depsgraph, ob, gpl, gpf);
			}
		}
	}
}

/* *************************************************** */

void BKE_gpencil_eval_geometry(Depsgraph *depsgraph,
                               bGPdata *gpd)
{
	DEG_debug_print_eval(depsgraph, __func__, gpd->id.name, gpd);
	int ctime = (int)DEG_get_ctime(depsgraph);

	/* update active frame */
	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		gpl->actframe = BKE_gpencil_layer_getframe(gpl, ctime, GP_GETFRAME_USE_PREV);
	}

	/* TODO: Move "derived_gpf" logic here from DRW_gpencil_populate_datablock()?
	 * This would be better than inventing our own logic for this stuff...
	 */

	/* TODO: Move the following code to "BKE_gpencil_eval_done()" (marked as an exit node)
	 * later when there's more happening here. For now, let's just keep this in here to avoid
	 * needing to have one more node slowing down evaluation...
	 */
	if (DEG_is_active(depsgraph)) {
		bGPdata *gpd_orig = (bGPdata *)DEG_get_original_id(&gpd->id);

		/* sync "actframe" changes back to main-db too,
		 * so that editing tools work with copy-on-write
		 * when the current frame changes
		 */
		for (bGPDlayer *gpl = gpd_orig->layers.first; gpl; gpl = gpl->next) {
			gpl->actframe = BKE_gpencil_layer_getframe(gpl, ctime, GP_GETFRAME_USE_PREV);
		}
	}
}


/* *************************************************** */

bool gpencil_modifier_unique_name(ListBase *modifiers, GreasePencilModifierData *gmd)
{
	if (modifiers && gmd) {
		const GreasePencilModifierTypeInfo *gmti = BKE_gpencil_modifierType_getInfo(gmd->type);
		return BLI_uniquename(modifiers, gmd, DATA_(gmti->name), '.', offsetof(GreasePencilModifierData, name), sizeof(gmd->name));
	}
	return false;
}
