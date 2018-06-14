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
 * The Original Code is: all of this file.
 *
 * ***** END GPL LICENSE BLOCK *****
 */
#ifndef __BKE_GREASEPENCIL_H__
#define __BKE_GREASEPENCIL_H__

/** \file BKE_greasepencil_modifier.h
 *  \ingroup bke
 */

#include "DNA_gpencil_modifier_types.h"     /* needed for all enum typdefs */
#include "BLI_compiler_attrs.h"
#include "BKE_customdata.h"

struct ID;
struct Depsgraph;
struct DerivedMesh;
struct bContext; /* NOTE: gp_bakeModifier() - called from UI - needs to create new datablocks, hence the need for this */
struct Mesh;
struct Object;
struct Scene;
struct ViewLayer;
struct ListBase;
struct bArmature;
struct Main;
struct ModifierData;
struct BMEditMesh;
struct DepsNodeHandle;
struct bGPDlayer;
struct bGPDframe;
struct bGPDstroke;

#ifdef TODO_GPENCIL_MODS
typedef struct GreasePencilModifierTypeInfo {
	/* The user visible name for this modifier */
	char name[32];

	/* The DNA struct name for the modifier data type, used to
	 * write the DNA data out.
	 */
	char structName[32];

	/* The size of the modifier data type, used by allocation. */
	int structSize;

	ModifierTypeType type;
	ModifierTypeFlag flags;


	/********************* Non-optional functions *********************/

	/* Copy instance data for this modifier type. Should copy all user
	 * level settings to the target modifier.
	 */
	void (*copyData)(const struct ModifierData *md, struct ModifierData *target);


	/******************* GP modifier functions *********************/

	/* Callback for GP "stroke" modifiers that operate on the
	 * shape and parameters of the provided strokes (e.g. Thickness, Noise, etc.)
	 *
	 * The gpl parameter contains the GP layer that the strokes come from.
	 * While access is provided to this data, you should not directly access
	 * the gpl->frames data from the modifier. Instead, use the gpf parameter
	 * instead.
	 *
	 * The gps parameter contains the GP stroke to operate on. This is usually a copy
	 * of the original (unmodified and saved to files) stroke data.
	 */
	void (*gp_deformStroke)(struct ModifierData *md, struct Depsgraph *depsgraph,
	                     struct Object *ob, struct bGPDlayer *gpl, struct bGPDstroke *gps);

	/* Callback for GP "geometry" modifiers that create extra geometry
	 * in the frame (e.g. Array)
	 *
	 * The gpf parameter contains the GP frame/strokes to operate on. This is
	 * usually a copy of the original (unmodified and saved to files) stroke data.
	 * Modifiers should only add any generated strokes to this frame (and not one accessed
	 * via the gpl parameter).
	 *
	 * The modifier_index parameter indicates where the modifier is
	 * in the modifier stack in relation to other modifiers.
	 */
	void (*gp_generateStrokes)(struct ModifierData *md, struct Depsgraph *depsgraph,
	                        struct Object *ob, struct bGPDlayer *gpl, struct bGPDframe *gpf);

	/* Bake-down GP modifier's effects into the GP datablock.
	 *
	 * This gets called when the user clicks the "Apply" button in the UI.
	 * As such, this callback needs to go through all layers/frames in the
	 * datablock, mutating the geometry and/or creating new datablocks/objects
	 */
	void (*gp_bakeModifier)(struct Main *bmain, struct Depsgraph *depsgraph,
                           struct ModifierData *md, struct Object *ob);

	/********************* Optional functions *********************/

	/* Initialize new instance data for this modifier type, this function
	 * should set modifier variables to their default values.
	 * 
	 * This function is optional.
	 */
	void (*initData)(struct ModifierData *md);

	/* Should return a CustomDataMask indicating what data this
	 * modifier needs. If (mask & (1 << (layer type))) != 0, this modifier
	 * needs that custom data layer. This function's return value can change
	 * depending on the modifier's settings.
	 *
	 * Note that this means extra data (e.g. vertex groups) - it is assumed
	 * that all modifiers need mesh data and deform modifiers need vertex
	 * coordinates.
	 *
	 * Note that this limits the number of custom data layer types to 32.
	 *
	 * If this function is not present or it returns 0, it is assumed that
	 * no extra data is needed.
	 *
	 * This function is optional.
	 */
	CustomDataMask (*requiredDataMask)(struct Object *ob, struct ModifierData *md);

	/* Free internal modifier data variables, this function should
	 * not free the md variable itself.
	 *
	 * This function is optional.
	 */
	void (*freeData)(struct ModifierData *md);

	/* Return a boolean value indicating if this modifier is able to be
	 * calculated based on the modifier data. This is *not* regarding the
	 * md->flag, that is tested by the system, this is just if the data
	 * validates (for example, a lattice will return false if the lattice
	 * object is not defined).
	 *
	 * This function is optional (assumes never disabled if not present).
	 */
	bool (*isDisabled)(struct ModifierData *md, int userRenderParams);

	/* Add the appropriate relations to the dependency graph.
	 *
	 * This function is optional.
	 */
	void (*updateDepsgraph)(struct ModifierData *md,
	                        const ModifierUpdateDepsgraphContext *ctx);
 
	/* Should return true if the modifier needs to be recalculated on time
	 * changes.
	 *
	 * This function is optional (assumes false if not present).
	 */
	bool (*dependsOnTime)(struct ModifierData *md);


	/* True when a deform modifier uses normals, the requiredDataMask
	 * cant be used here because that refers to a normal layer where as
	 * in this case we need to know if the deform modifier uses normals.
	 * 
	 * this is needed because applying 2 deform modifiers will give the
	 * second modifier bogus normals.
	 * */
	bool (*dependsOnNormals)(struct ModifierData *md);


	/* Should call the given walk function on with a pointer to each Object
	 * pointer that the modifier data stores. This is used for linking on file
	 * load and for unlinking objects or forwarding object references.
	 *
	 * This function is optional.
	 */
	void (*foreachObjectLink)(struct ModifierData *md, struct Object *ob,
	                          ObjectWalkFunc walk, void *userData);

	/* Should call the given walk function with a pointer to each ID
	 * pointer (i.e. each datablock pointer) that the modifier data
	 * stores. This is used for linking on file load and for
	 * unlinking datablocks or forwarding datablock references.
	 *
	 * This function is optional. If it is not present, foreachObjectLink
	 * will be used.
	 */
	void (*foreachIDLink)(struct ModifierData *md, struct Object *ob,
	                      IDWalkFunc walk, void *userData);

	/* Should call the given walk function for each texture that the
	 * modifier data stores. This is used for finding all textures in
	 * the context for the UI.
	 *
	 * This function is optional. If it is not present, it will be
	 * assumed the modifier has no textures.
	 */
	void (*foreachTexLink)(struct ModifierData *md, struct Object *ob,
	                       TexWalkFunc walk, void *userData);
} ModifierTypeInfo;

/* Initialize modifier's global data (type info and some common global storages). */
void BKE_modifier_init(void);

const ModifierTypeInfo *modifierType_getInfo(ModifierType type);

struct ModifierData  *modifier_new(int type);
void          modifier_free_ex(struct ModifierData *md, const int flag);
void          modifier_free(struct ModifierData *md);
#endif /* TODO_GPENCIL_MODS */

bool          greasepencil_modifier_unique_name(struct ListBase *modifiers, struct GreasePencilModifierData *gmd);

#endif

