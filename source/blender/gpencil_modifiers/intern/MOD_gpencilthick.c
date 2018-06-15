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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2017, Blender Foundation
 * This is a new part of Blender
 *
 * Contributor(s): Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/modifiers/intern/MOD_gpencilthick.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_meshdata_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_gpencil_modifier_types.h"

#include "BLI_utildefines.h"

#include "BKE_colortools.h"
#include "BKE_context.h"
#include "BKE_deform.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_modifier.h"

#include "DEG_depsgraph.h"

#include "MOD_modifiertypes.h"
#include "MOD_gpencil_util.h"

static void initData(ModifierData *md)
{
	ThickGreasePencilModifierData *gpmd = (ThickGreasePencilModifierData *)md;
	gpmd->pass_index = 0;
	gpmd->thickness = 0;
	gpmd->layername[0] = '\0';
	gpmd->vgname[0] = '\0';
	gpmd->curve_thickness = curvemapping_add(1, 0.0f, 0.0f, 1.0f, 1.0f);
	if (gpmd->curve_thickness) {
		curvemapping_initialize(gpmd->curve_thickness);
	}
}

static void freeData(ModifierData *md)
{
	ThickGreasePencilModifierData *gpmd = (ThickGreasePencilModifierData *)md;

	if (gpmd->curve_thickness) {
		curvemapping_free(gpmd->curve_thickness);
	}
}

static void copyData(const ModifierData *md, ModifierData *target)
{
	ThickGreasePencilModifierData *gmd = (ThickGreasePencilModifierData *)md;
	ThickGreasePencilModifierData *tgmd = (ThickGreasePencilModifierData *)target;

	if (tgmd->curve_thickness != NULL) {
		curvemapping_free(tgmd->curve_thickness);
		tgmd->curve_thickness = NULL;
	}

	modifier_copyData_generic(md, target);

	tgmd->curve_thickness = curvemapping_copy(gmd->curve_thickness);
}

/* change stroke thickness */
static void gp_deformStroke(
        ModifierData *md, Depsgraph *UNUSED(depsgraph),
        Object *ob, bGPDlayer *gpl, bGPDstroke *gps)
{
	ThickGreasePencilModifierData *mmd = (ThickGreasePencilModifierData *)md;
	int vindex = defgroup_name_index(ob, mmd->vgname);

	if (!is_stroke_affected_by_modifier(ob, 
	        mmd->layername, mmd->pass_index, 3, gpl, gps,
	        mmd->flag & GP_THICK_INVERT_LAYER, mmd->flag & GP_THICK_INVERT_PASS))
	{
		return;
	}

	/* if normalize, set stroke thickness */
	if (mmd->flag & GP_THICK_NORMALIZE) {
		gps->thickness = mmd->thickness;
	}

	for (int i = 0; i < gps->totpoints; i++) {
		bGPDspoint *pt = &gps->points[i];
		MDeformVert *dvert = &gps->dvert[i];
		float curvef = 1.0f;
		/* verify vertex group */
		float weight = get_modifier_point_weight(dvert, (int)(!(mmd->flag & GP_THICK_INVERT_VGROUP) == 0), vindex);
		if (weight < 0) {
			continue;
		}

		if (mmd->flag & GP_THICK_NORMALIZE) {
			pt->pressure = 1.0f;
		}
		else {
			if ((mmd->flag & GP_THICK_CUSTOM_CURVE) && (mmd->curve_thickness)) {
				/* normalize value to evaluate curve */
				float value = (float)i / (gps->totpoints - 1);
				curvef = curvemapping_evaluateF(mmd->curve_thickness, 0, value);
			}

			pt->pressure += mmd->thickness * weight * curvef;
			CLAMP(pt->strength, 0.0f, 1.0f);
		}
	}
}

static void gp_bakeModifier(
		struct Main *UNUSED(bmain), Depsgraph *depsgraph,
        ModifierData *md, Object *ob)
{
	bGPdata *gpd = ob->data;

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				gp_deformStroke(md, depsgraph, ob, gpl, gps);
			}
		}
	}
}

ModifierTypeInfo modifierType_Gpencil_Thick = {
	/* name */              "Thickness",
	/* structName */        "ThickGreasePencilModifierData",
	/* structSize */        sizeof(ThickGreasePencilModifierData),
	/* type */              eModifierTypeType_Gpencil,
	/* flags */             eGreasePencilModifierTypeFlag_GpencilMod | eGreasePencilModifierTypeFlag_SupportsEditmode,

	/* copyData */          copyData,

	/* deformVerts_DM */    NULL,
	/* deformMatrices_DM */ NULL,
	/* deformVertsEM_DM */  NULL,
	/* deformMatricesEM_DM*/NULL,
	/* applyModifier_DM */  NULL,
	/* applyModifierEM_DM */NULL,

	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,

	/* gp_deformStroke */      gp_deformStroke,
	/* gp_generateStrokes */   NULL,
	/* gp_bakeModifier */    gp_bakeModifier,

	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          freeData,
	/* isDisabled */        NULL,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ NULL,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
