"""
OASIS Avatar Pipeline — Blender Batch OBJ Importer (Addon)
============================================================
A 3D Pipeline Technical Director addon for Blender 3.x / 4.x.

INSTALLATION (easiest method):
  1. Open Blender
  2. Go to Edit → Preferences → Add-ons → Install…
  3. Select this file: blender_oasis_importer.py
  4. Enable the checkbox next to "OASIS Avatar Pipeline"
  5. Press N in the 3D Viewport → find the "OASIS" tab
  6. Set your asset folder → click "Import All Avatars"

ALTERNATIVE (paste method):
  1. Open Blender → Scripting tab → New
  2. Paste this entire file
  3. Press ▶ (Run Script)
  4. A file browser will open — navigate to your avatar folder and confirm
  5. All .obj files are imported automatically

WHAT IT DOES:
  • Opens a file browser to pick your asset folder (no code editing!)
  • Scans recursively for .obj files
  • Imports each into named Collections matching character names
  • Links Diffuse, Normal, Metallic, Roughness, Emission textures
  • Applies Principled BSDF with cyberpunk emission glow
  • Normalizes all avatars to ~1.8m human scale
  • Corrects Y-up → Z-up orientation mismatches
  • Creates a master "OASIS_Avatars" collection

Author: Buffy (Codebuff / Freebuff)
License: Public Domain
"""

import bpy
import os
import re
import math
from pathlib import Path
from mathutils import Vector, Matrix

bl_info = {
    "name": "OASIS Avatar Pipeline",
    "author": "Buffy / Codebuff",
    "version": (1, 1, 0),
    "blender": (3, 0, 0),
    "location": "View3D > Sidebar > OASIS tab",
    "description": "Batch import OBJ avatars with auto-hierarchy, materials, scale fix, and emission glow",
    "category": "Import-Export",
}

# ═══════════════════════════════════════════════════════════════
#  SETTINGS (editable from the UI panel)
# ═══════════════════════════════════════════════════════════════

# Persistent settings stored in Blender preferences
class OASIS_Preferences(bpy.types.AddonPreferences):
    bl_idname = __name__

    asset_dir: bpy.props.StringProperty(
        name="Asset Directory",
        description="Root folder containing your .obj avatar files",
        default="//",
        subtype='DIR_PATH',
    )
    target_height: bpy.props.FloatProperty(
        name="Target Height (m)",
        description="Normalize all avatars to this height",
        default=1.8, min=0.5, max=5.0,
    )
    fix_orientation: bpy.props.BoolProperty(
        name="Fix Y-up → Z-up",
        description="Auto-detect and correct orientation mismatches",
        default=True,
    )
    emission_strength: bpy.props.FloatProperty(
        name="Emission Strength",
        description="Glow intensity for OASIS cyberpunk materials",
        default=5.0, min=0.0, max=50.0,
    )
    master_collection: bpy.props.StringProperty(
        name="Master Collection",
        description="Root collection name",
        default="OASIS_Avatars",
    )

    def draw(self, context):
        layout = self.layout
        layout.prop(self, "asset_dir")
        layout.prop(self, "target_height")
        layout.prop(self, "fix_orientation")
        layout.prop(self, "emission_strength")
        layout.prop(self, "master_collection")


def get_prefs():
    return bpy.context.preferences.addons[__name__].preferences


# ═══════════════════════════════════════════════════════════════
#  CHARACTER NAME MAP (60+ OASIS/game/comic/movie avatars)
# ═══════════════════════════════════════════════════════════════

CHARACTER_NAMES = {
    "parzival": "Parzival", "wade": "Parzival",
    "art3mis": "Art3mis", "samantha": "Art3mis",
    "aech": "Aech", "helen": "Aech",
    "daito": "Daito", "toshiro": "Daito",
    "sho": "Sho", "zhou": "Sho",
    "anorak": "Anorak", "halliday": "Anorak",
    "curator": "The Curator", "og ": "The Curator", "morrow": "The Curator",
    "sorrento": "Sorrento", "nolan": "Sorrento",
    "i-rok": "i-R0k", "irok": "i-R0k",
    "tracer": "Tracer", "chun-li": "Chun-Li", "chunli": "Chun-Li",
    "ryu": "Ryu", "sagat": "Sagat", "blanka": "Blanka",
    "goro": "Goro", "raiden": "Raiden", "sub-zero": "Sub-Zero", "subzero": "Sub-Zero",
    "scorpion": "Scorpion", "kitana": "Kitana",
    "masterchief": "Master Chief", "master chief": "Master Chief",
    "lara": "Lara Croft", "tombraider": "Lara Croft",
    "duke": "Duke Nukem", "raynor": "Jim Raynor",
    "sonic": "Sonic", "rash": "Rash", "zitz": "Zitz", "pimple": "Pimple",
    "attikus": "Attikus", "ambra": "Ambra",
    "dragon": "El Dragón", "el dragon": "El Dragón", "jynx": "Jynx",
    "batman": "Batman", "joker": "The Joker", "harley": "Harley Quinn",
    "deathstroke": "Deathstroke", "deadshot": "Deadshot",
    "arkham": "Arkham Knight", "flash": "The Flash",
    "aquaman": "Aquaman", "batgirl": "Batgirl", "supergirl": "Supergirl",
    "comedian": "The Comedian", "spawn": "Spawn",
    "iron giant": "Iron Giant", "gundam": "RX-78-2 Gundam", "rx-78": "RX-78-2 Gundam",
    "mechagodzilla": "Mechagodzilla", "godzilla": "Mechagodzilla",
    "freddy": "Freddy Krueger", "krueger": "Freddy Krueger",
    "jason": "Jason Voorhees", " Voorhees": "Jason Voorhees",
    "chucky": "Chucky", "robocop": "RoboCop",
    "beetlejuice": "Beetlejuice", "king kong": "King Kong", "kong": "King Kong",
    "xenomorph": "Xenomorph", "facehugger": "Facehugger", "alien": "Xenomorph",
    "hello kitty": "Hello Kitty", "kitty": "Hello Kitty",
    "badtz": "Badtz-Maru", "marvin": "Marvin the Martian", "martian": "Marvin the Martian",
}

TEXTURE_PATTERNS = {
    "diffuse":   ["diffuse", "diff", "albedo", "basecolor", "base_color", "color", "col"],
    "normal":    ["normal", "nrm", "norm", "bump"],
    "metallic":  ["metallic", "metal", "met"],
    "roughness": ["roughness", "rough", "rgh"],
    "emission":  ["emission", "emissive", "emit", "glow", "self_illum", "selfillum"],
    "ao":        ["ambient_occlusion", "ao", "occlusion"],
}

# ═══════════════════════════════════════════════════════════════
#  UTILITY FUNCTIONS
# ═══════════════════════════════════════════════════════════════

def log(msg, level="INFO"):
    prefix = f"[OASIS {level}]"
    print(f"{prefix} {msg}")
    # Also show in Blender's info bar
    def _draw(self, context):
        self.layout.label(text=f"OASIS: {msg}")


def resolve_character_name(filepath):
    basename = Path(filepath).stem.lower()
    clean = re.sub(r'[_\-](low|high|lod\d+|final|v\d+|pub|copy)$', '', basename)
    clean = re.sub(r'[\s_\-]+', ' ', clean).strip()
    for pattern, name in CHARACTER_NAMES.items():
        if pattern in clean:
            return name
    return clean.replace('_', ' ').replace('-', ' ').title()


def scan_for_textures(directory, asset_root):
    """Scan for textures in directory and common subdirectories."""
    exts = {'.png', '.jpg', '.jpeg', '.tga', '.tif', '.tiff', '.bmp', '.exr', '.hdr'}
    found = {}
    search_paths = [Path(directory)]
    for sub in ["textures", "materials", "../textures"]:
        for base in [Path(directory), Path(asset_root)]:
            candidate = base / sub
            if candidate.is_dir():
                search_paths.append(candidate)

    all_textures = []
    for sp in search_paths:
        if sp.is_dir():
            try:
                for f in sp.iterdir():
                    if f.suffix.lower() in exts and f.is_file():
                        all_textures.append(f)
            except PermissionError:
                pass

    for tex_file in all_textures:
        fname = tex_file.stem.lower()
        for tex_type, keywords in TEXTURE_PATTERNS.items():
            for kw in keywords:
                pattern = r'(?:^|[\s_\-/])' + re.escape(kw) + r'(?:[\s_\-/]|$)'
                if re.search(pattern, fname):
                    if tex_type not in found:
                        found[tex_type] = str(tex_file)
                    break
    return found


def get_or_create_collection(name, parent=None):
    if name in bpy.data.collections:
        coll = bpy.data.collections[name]
    else:
        coll = bpy.data.collections.new(name)
        if parent is None:
            bpy.context.scene.collection.children.link(coll)
        else:
            parent.children.link(coll)
    return coll


def link_to_collection(obj, collection):
    for coll in obj.users_collection:
        coll.objects.unlink(obj)
    collection.objects.link(obj)


def get_mesh_bounds(obj):
    if obj.type != 'MESH':
        return Vector((0, 0, 0))
    bbox = [Vector(c) for c in obj.bound_box]
    mn = Vector((min(v[i] for v in bbox) for i in range(3)))
    mx = Vector((max(v[i] for v in bbox) for i in range(3)))
    return mx - mn


# ═══════════════════════════════════════════════════════════════
#  MATERIAL BUILDER
# ═══════════════════════════════════════════════════════════════

def build_principled_material(mat_name, texture_map, emission_str=5.0, has_emission=False):
    if mat_name in bpy.data.materials:
        bpy.data.materials.remove(bpy.data.materials[mat_name])

    mat = bpy.data.materials.new(name=mat_name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (800, 0)

    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (400, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    use_new_api = "Emission Color" in bsdf.inputs
    emit_socket = "Emission Color" if use_new_api else "Emission"

    y = 0

    def add_tex(filepath, label, socket_name, colorspace=None):
        if not filepath or not os.path.isfile(filepath):
            return None
        n = nodes.new('ShaderNodeTexImage')
        n.location = (-200, y)
        n.label = label
        try:
            img = bpy.data.images.load(filepath)
            if colorspace:
                img.colorspace_settings.name = colorspace
            n.image = img
        except Exception as e:
            log(f"  Could not load {filepath}: {e}", "WARN")
            return None
        links.new(n.outputs['Color'], bsdf.inputs[socket_name])
        return n

    if "diffuse" in texture_map:
        log(f"  Diffuse: {Path(texture_map['diffuse']).name}")
        add_tex(texture_map["diffuse"], "Diffuse", "Base Color", "sRGB")
        y -= 300

    if "normal" in texture_map:
        log(f"  Normal: {Path(texture_map['normal']).name}")
        nn = nodes.new('ShaderNodeTexImage')
        nn.location = (-400, y); nn.label = "Normal"
        try:
            img = bpy.data.images.load(texture_map["normal"])
            img.colorspace_settings.name = "Non-Color"
            nn.image = img
        except Exception as e:
            log(f"  Could not load normal: {e}", "WARN")
        nm = nodes.new('ShaderNodeNormalMap')
        nm.location = (-100, y); nm.label = "NormalMap"
        links.new(nn.outputs['Color'], nm.inputs['Color'])
        links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
        y -= 300

    if "metallic" in texture_map:
        log(f"  Metallic: {Path(texture_map['metallic']).name}")
        add_tex(texture_map["metallic"], "Metallic", "Metallic", "Non-Color")
        y -= 300

    if "roughness" in texture_map:
        log(f"  Roughness: {Path(texture_map['roughness']).name}")
        add_tex(texture_map["roughness"], "Roughness", "Roughness", "Non-Color")
        y -= 300

    if "emission" in texture_map:
        log(f"  Emission: {Path(texture_map['emission']).name}")
        add_tex(texture_map["emission"], "Emission", emit_socket, "sRGB")
        if "Emission Strength" in bsdf.inputs:
            bsdf.inputs['Emission Strength'].default_value = emission_str
        y -= 300
    elif has_emission and "diffuse" in texture_map:
        add_tex(texture_map["diffuse"], "Emit (from Diffuse)", emit_socket, "sRGB")
        if "Emission Strength" in bsdf.inputs:
            bsdf.inputs['Emission Strength'].default_value = emission_str * 0.5

    if "roughness" not in texture_map:
        bsdf.inputs['Roughness'].default_value = 0.4

    return mat


# ═══════════════════════════════════════════════════════════════
#  OBJ IMPORTER
# ═══════════════════════════════════════════════════════════════

def import_obj(filepath, collection, character_name, prefs):
    log(f"Importing: {Path(filepath).name} → {character_name}")

    existing = set(bpy.data.objects)
    prefs_dir = Path(prefs.asset_dir).resolve()

    bpy.ops.import_scene.obj(
        filepath=str(filepath),
        axis_forward='-Z', axis_up='Y',
        use_split_objects=True, use_split_groups=True,
        use_smooth_groups=True, use_uvs=True,
        use_materials=True, global_clamp_size=0.0,
    )

    imported = [o for o in bpy.data.objects if o not in existing]
    if not imported:
        log(f"  No objects imported from {filepath}", "WARN")
        return []

    # Find root (largest mesh)
    root = imported[0]
    for obj in imported:
        if obj.type == 'MESH' and obj.type == 'MESH':
            try:
                if len(obj.data.vertices) > len(root.data.vertices):
                    root = obj
            except:
                pass

    root.name = character_name
    if root.data:
        root.data.name = f"{character_name}_Mesh"

    for obj in imported:
        link_to_collection(obj, collection)
        if obj != root:
            obj.name = f"{character_name}_{obj.name}"
            obj.parent = root
            obj.matrix_parent_inverse = root.matrix_world.inverted()

    # Fix orientation
    if prefs.fix_orientation:
        dims = get_mesh_bounds(root)
        if dims.y > dims.z * 1.3 and dims.y > dims.x * 1.3:
            log(f"  Fixing Y-up → Z-up rotation")
            rot = Matrix.Rotation(-math.pi / 2, 4, 'X')
            for obj in imported:
                if obj.type == 'MESH':
                    obj.data.transform(rot)
        elif dims.x > dims.z * 1.3 and dims.x > dims.y * 1.3:
            log(f"  Fixing X-up → Z-up rotation")
            rot = Matrix.Rotation(math.pi / 2, 4, 'Z')
            for obj in imported:
                if obj.type == 'MESH':
                    obj.data.transform(rot)

    # Scale to target
    dims = get_mesh_bounds(root)
    height = max(dims.x, dims.y, dims.z)
    if height > 0:
        scale = prefs.target_height / height
        log(f"  Scaling by {scale:.4f} (height {height:.3f}m → {prefs.target_height}m)")
        for obj in imported:
            if obj.type == 'MESH':
                obj.data.transform(Matrix.Scale(scale, 4))

    # Detect textures
    tex_map = scan_for_textures(str(Path(filepath).parent), str(prefs_dir))
    has_emission = any(kw in str(filepath).lower() for kw in ['glow', 'neon', 'cyber', 'emission'])

    # Apply material
    if root.type == 'MESH' and root.materials:
        for i, mat_slot in enumerate(root.materials):
            if mat_slot is None:
                continue
            new_mat = build_principled_material(
                f"OASIS_{character_name}_{mat_slot.name}",
                tex_map, prefs.emission_strength, has_emission
            )
            root.materials[i] = new_mat
            for child in imported:
                if child != root and child.type == 'MESH':
                    for j, cm in enumerate(child.materials):
                        if cm and cm.name == mat_slot.name:
                            child.materials[i] = new_mat

    # Smooth shading
    for obj in imported:
        if obj.type == 'MESH':
            for poly in obj.data.polygons:
                poly.use_smooth = True

    log(f"  ✓ {character_name}: {len(imported)} meshes, {len(tex_map)} textures")
    return imported


# ═══════════════════════════════════════════════════════════════
#  BATCH IMPORT (called from operator or file browser)
# ═══════════════════════════════════════════════════════════════

def run_batch_import(directory=None):
    prefs = get_prefs()
    asset_dir = directory or str(Path(prefs.asset_dir).resolve())

    log("=" * 50)
    log("OASIS Avatar Pipeline — Batch Import")
    log("=" * 50)
    log(f"Folder: {asset_dir}")

    if not Path(asset_dir).is_dir():
        log(f"ERROR: Directory not found: {asset_dir}", "ERROR")
        return {'CANCELLED'}

    master = get_or_create_collection(prefs.master_collection)

    # Find OBJ files
    obj_files = sorted(Path(asset_dir).rglob("*.obj"))
    if not obj_files:
        obj_files = sorted(Path(asset_dir).rglob("*.OBJ"))
    if not obj_files:
        log(f"No .obj files found in {asset_dir}", "ERROR")
        return {'CANCELLED'}

    log(f"Found {len(obj_files)} OBJ files")

    # Group by directory
    groups = {}
    for f in obj_files:
        rel = f.parent.relative_to(asset_dir)
        key = str(rel).split(os.sep)[0] if str(rel) != '.' else f.stem
        groups.setdefault(key, []).append(f)

    total = 0
    for gname, files in sorted(groups.items()):
        gcoll = get_or_create_collection(gname, parent=master)
        for f in files:
            cname = resolve_character_name(f)
            if len(files) > 1:
                v = f.stem.lower()
                suffix = "_LOD" if 'lod' in v or 'low' in v else "_High" if 'high' in v else f"_{f.stem}"
                cname = f"{cname}{suffix}"
            mc = get_or_create_collection(cname, parent=gcoll)
            try:
                imported = import_obj(f, mc, cname.split('_')[0] if '_' in cname else cname, prefs)
                total += len(imported)
            except Exception as e:
                log(f"  FAILED {f.name}: {e}", "ERROR")

    log("=" * 50)
    log(f"DONE: {total} objects imported into '{prefs.master_collection}'")
    log("=" * 50)
    return {'FINISHED'}


# ═══════════════════════════════════════════════════════════════
#  BLENDER OPERATORS
# ═══════════════════════════════════════════════════════════════

class OASIS_OT_ImportAll(bpy.types.Operator):
    """Import all OBJ avatars from the configured directory"""
    bl_idname = "oasis.import_all"
    bl_label = "Import All Avatars"
    bl_description = "Scan the asset directory and import all .obj avatar files"

    directory: bpy.props.StringProperty(subtype='DIR_PATH')

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {'RUNNING_MODAL'}

    def execute(self, context):
        return run_batch_import(self.directory)


class OASIS_OT_QuickImport(bpy.types.Operator):
    """Import from the pre-configured directory (no file browser)"""
    bl_idname = "oasis.quick_import"
    bl_label = "Quick Import"
    bl_description = "Import from the directory set in preferences (no file picker)"

    def execute(self, context):
        return run_batch_import()


class OASIS_OT_CleanAll(bpy.types.Operator):
    """Remove all OASIS imported collections"""
    bl_idname = "oasis.clean_all"
    bl_label = "Clean All"
    bl_description = "Delete all OASIS_Avatars collections and their contents"

    def execute(self, context):
        prefs = get_prefs()
        if prefs.master_collection in bpy.data.collections:
            coll = bpy.data.collections[prefs.master_collection]
            # Remove all objects first
            def remove_recursive(c):
                for child in c.children:
                    remove_recursive(child)
                for obj in list(c.objects):
                    bpy.data.objects.remove(obj, do_unlink=True)
            remove_recursive(coll)
            bpy.data.collections.remove(coll)
            log("All OASIS collections removed")
        return {'FINISHED'}


# ═══════════════════════════════════════════════════════════════
#  UI PANEL (Sidebar > OASIS tab)
# ═══════════════════════════════════════════════════════════════

class OASIS_PT_Panel(bpy.types.Panel):
    """OASIS Avatar Pipeline panel"""
    bl_label = "OASIS Avatar Pipeline"
    bl_idname = "OASIS_PT_main"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'OASIS'

    def draw(self, context):
        layout = self.layout
        prefs = get_prefs()

        # File browser import
        box = layout.box()
        box.label(text="📁 Import Avatars", icon='IMPORT')
        row = box.row(align=True)
        row.scale_y = 1.8
        row.operator("oasis.import_all", icon='FILE_FOLDER')

        # Quick import
        box2 = layout.box()
        box2.label(text="⚡ Quick Import", icon='PLAY')
        row2 = box2.row(align=True)
        row2.scale_y = 1.4
        row2.operator("oasis.quick_import", icon='PLAY')

        # Current directory display
        box3 = layout.box()
        box3.label(text="📂 Asset Directory:", icon='FILE_FOLDER')
        box3.prop(prefs, "asset_dir", text="")

        # Settings
        box4 = layout.box()
        box4.label(text="⚙ Settings", icon='PREFERENCES')
        box4.prop(prefs, "target_height")
        box4.prop(prefs, "fix_orientation")
        box4.prop(prefs, "emission_strength")
        box4.prop(prefs, "master_collection")

        # Cleanup
        layout.separator()
        row3 = layout.row()
        row3.alert = True
        row3.operator("oasis.clean_all", icon='TRASH')


# ═══════════════════════════════════════════════════════════════
#  REGISTRATION
# ═══════════════════════════════════════════════════════════════

classes = (
    OASIS_Preferences,
    OASIS_OT_ImportAll,
    OASIS_OT_QuickImport,
    OASIS_OT_CleanAll,
    OASIS_PT_Panel,
)

def register():
    for cls in classes:
        bpy.utils.register_class(cls)

def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)

if __name__ == "__main__":
    register()
    log("OASIS Avatar Pipeline addon registered! Look in View3D > Sidebar > OASIS tab")
