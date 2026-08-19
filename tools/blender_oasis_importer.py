"""
OASIS Avatar Pipeline — Blender Batch OBJ Importer
====================================================
A 3D Pipeline Technical Director script for Blender 3.x / 4.x.

Automates the full import → organize → shade → fix pipeline for
Ready Player One / OASIS-style avatar meshes.

USAGE:
  1. Open Blender (3.x or 4.x).
  2. Go to Scripting tab → New → paste this script.
  3. Edit CONFIG below to point at your asset directories.
  4. Run the script (▶ or Alt+P).

WHAT IT DOES:
  • Scans target directories for .obj files
  • Imports each into its own Collection named after the character
  • Detects and links Diffuse, Normal, Metallic, Roughness, Emission maps
  • Applies Principled BSDF with cyberpunk-ready emission
  • Normalizes all avatars to ~1.8m human scale
  • Corrects Y-up → Z-up orientation mismatches
  • Creates a master "OASIS_Avatars" collection as the root

Author: Buffy (Codebuff / Freebuff)
License: Public Domain
"""

import bpy
import bmesh
import os
import re
import sys
import math
from pathlib import Path
from mathutils import Vector, Matrix

# ═══════════════════════════════════════════════════════════════
#  CONFIGURATION — Edit these paths before running
# ═══════════════════════════════════════════════════════════════

# Root directory containing your .obj avatar files.
# Subdirectories are scanned recursively.
# Each top-level subdirectory becomes a Character Group.
ASSET_DIR = "/path/to/oasis_avatars"

# Where textures live. The script searches:
#   1. Same directory as the .obj file
#   2. A "textures/" subdirectory
#   3. A "materials/" subdirectory
#   4. A flat "textures/" folder at the asset root
TEXTURE_SEARCH_DIRS = [
    "textures",
    "materials",
    "../textures",
]

# Target avatar height in meters (average human)
TARGET_HEIGHT_M = 1.8

# Whether to apply Y-up to Z-up correction
# (OBJ convention is Z-up but some exports are Y-up)
FIX_ORIENTATION = True

# Emission strength for OASIS glow materials (default 5.0)
DEFAULT_EMISSION_STRENGTH = 5.0

# Master collection name
MASTER_COLLECTION = "OASIS_Avatars"

# Known character name overrides: maps filename keywords → display name
# Add your own! Keys are lowercase match patterns.
CHARACTER_NAMES = {
    # OASIS originals
    "parzival":    "Parzival",
    "wade":        "Parzival",
    "art3mis":     "Art3mis",
    "samantha":    "Art3mis",
    "aech":        "Aech",
    "helen":       "Aech",
    "daito":       "Daito",
    "toshiro":     "Daito",
    "sho":         "Sho",
    "zhou":        "Sho",
    "anorak":      "Anorak",
    "halliday":    "Anorak",
    "curator":     "The Curator",
    "og":          "The Curator",
    "morrow":      "The Curator",
    "sorrento":    "Sorrento",
    "nolan":       "Sorrento",
    "i-rok":       "i-R0k",
    "irok":        "i-R0k",
    # Video game avatars
    "tracer":      "Tracer",
    "chun-li":     "Chun-Li",
    "chunli":      "Chun-Li",
    "ryu":         "Ryu",
    "sagat":       "Sagat",
    "blanka":      "Blanka",
    "goro":        "Goro",
    "raiden":      "Raiden",
    "sub-zero":    "Sub-Zero",
    "subzero":     "Sub-Zero",
    "scorpion":    "Scorpion",
    "kitana":      "Kitana",
    "masterchief": "Master Chief",
    "master chief":"Master Chief",
    "lara":        "Lara Croft",
    "tombraider":  "Lara Croft",
    "duke":        "Duke Nukem",
    "raynor":      "Jim Raynor",
    "sonic":       "Sonic",
    "rash":        "Rash",
    "zitz":        "Zitz",
    "pimple":      "Pimple",
    "attikus":     "Attikus",
    "ambra":       "Ambra",
    "dragon":      "El Dragón",
    "el dragon":   "El Dragón",
    "jynx":        "Jynx",
    # Comics
    "batman":      "Batman",
    "joker":       "The Joker",
    "harley":      "Harley Quinn",
    "deathstroke": "Deathstroke",
    "deadshot":    "Deadshot",
    "arkham":      "Arkham Knight",
    "flash":       "The Flash",
    "aquaman":     "Aquaman",
    "batgirl":     "Batgirl",
    "supergirl":   "Supergirl",
    "comedian":    "The Comedian",
    "spawn":       "Spawn",
    # Movies / sci-fi
    "iron giant":  "Iron Giant",
    "gundam":      "RX-78-2 Gundam",
    "rx-78":       "RX-78-2 Gundam",
    "mechagodzilla":"Mechagodzilla",
    "godzilla":    "Mechagodzilla",
    "freddy":      "Freddy Krueger",
    "krueger":     "Freddy Krueger",
    "jason":       "Jason Voorhees",
    " Voorhees":   "Jason Voorhees",
    "chucky":      "Chucky",
    "robocop":     "RoboCop",
    "beetlejuice": "Beetlejuice",
    "king kong":   "King Kong",
    "kong":        "King Kong",
    "xenomorph":   "Xenomorph",
    "facehugger":  "Facehugger",
    "alien":       "Xenomorph",
    # Retro / animated
    "hello kitty": "Hello Kitty",
    "kitty":       "Hello Kitty",
    "badtz":       "Badtz-Maru",
    "marvin":      "Marvin the Martian",
    "martian":     "Marvin the Martian",
}

# Material keyword → texture type mapping
# Searches filenames for these patterns (case-insensitive)
TEXTURE_PATTERNS = {
    "diffuse":     ["diffuse", "diff", "albedo", "basecolor", "base_color", "color", "col"],
    "normal":      ["normal", "nrm", "norm", "bump"],
    "metallic":    ["metallic", "metal", "met"],
    "roughness":   ["roughness", "rough", "rgh"],
    "emission":    ["emission", "emissive", "emit", "glow", "self_illum", "selfillum", "hdr"],
    "ao":          ["ambient_occlusion", "ao", "occlusion"],
    "height":      ["height", "displacement", "disp", "dis"],
    "opacity":     ["opacity", "alpha", "transparency", "mask"],
}

# ═══════════════════════════════════════════════════════════════
#  UTILITY FUNCTIONS
# ═══════════════════════════════════════════════════════════════

def log(msg, level="INFO"):
    """Print with timestamp."""
    prefix = f"[OASIS Import {level}]"
    print(f"{prefix} {msg}")


def resolve_character_name(filepath):
    """Map a filename to a clean character display name."""
    basename = Path(filepath).stem.lower()
    # Remove common suffixes
    clean = re.sub(r'[_\-](low|high|lod\d+|final|v\d+|pub|copy|_)$', '', basename)
    clean = re.sub(r'[\s_\-]+', ' ', clean).strip()

    # Check overrides
    for pattern, name in CHARACTER_NAMES.items():
        if pattern in clean:
            return name

    # Fallback: title-case the filename
    return clean.replace('_', ' ').replace('-', ' ').title()


def scan_for_textures(directory):
    """
    Scan a directory (and configured subdirs) for texture files.
    Returns a dict: { texture_type: filepath }
    """
    texture_extensions = {'.png', '.jpg', '.jpeg', '.tga', '.tif', '.tiff', '.bmp', '.exr', '.hdr'}
    found = {}

    search_paths = [Path(directory)]
    for sub in TEXTURE_SEARCH_DIRS:
        candidate = Path(directory) / sub
        if candidate.is_dir():
            search_paths.append(candidate)
        # Also check parent dirs
        candidate2 = Path(directory).parent / sub
        if candidate2.is_dir():
            search_paths.append(candidate2)

    # Flatten: scan all texture files
    all_textures = []
    for sp in search_paths:
        if sp.is_dir():
            for f in sp.iterdir():
                if f.suffix.lower() in texture_extensions and f.is_file():
                    all_textures.append(f)

    # Classify each texture
    for tex_file in all_textures:
        fname = tex_file.stem.lower()
        for tex_type, keywords in TEXTURE_PATTERNS.items():
            for kw in keywords:
                # Match with word boundaries or separators
                pattern = r'(?:^|[\s_\-/])' + re.escape(kw) + r'(?:[\s_\-/]|$)'
                if re.search(pattern, fname):
                    if tex_type not in found:
                        found[tex_type] = str(tex_file)
                    break

    return found


def get_or_create_collection(name, parent=None):
    """Get or create a Blender collection under the given parent."""
    if name in bpy.data.collections:
        coll = bpy.data.collections[name]
    else:
        coll = bpy.data.collections.new(name)
        if parent is None:
            # Link to scene
            scene_coll = bpy.context.scene.collection
            scene_coll.children.link(coll)
        else:
            parent.children.link(coll)
    return coll


def link_to_collection(obj, collection):
    """Link an object to a collection, removing from all others."""
    for coll in obj.users_collection:
        coll.objects.unlink(obj)
    collection.objects.link(obj)


def get_mesh_bounds(obj):
    """Get the bounding box dimensions of an object (in local space)."""
    if obj.type != 'MESH':
        return Vector((0, 0, 0))
    bbox = [Vector(corner) for corner in obj.bound_box]
    min_co = Vector((min(v[i] for v in bbox) for i in range(3)))
    max_co = Vector((max(v[i] for v in bbox) for i in range(3)))
    return max_co - min_co


def compute_scale_factor(obj):
    """
    Compute scale factor needed to make the object TARGET_HEIGHT_M tall.
    Uses the largest Y or Z extent as the "height" axis.
    """
    dims = get_mesh_bounds(obj)
    # OBJ files: Z-up → height is Z. FBX/Blender: Y-up → height is Y.
    # We use whichever is tallest.
    height = max(dims.x, dims.y, dims.z)
    if height <= 0:
        return 1.0
    return TARGET_HEIGHT_M / height


# ═══════════════════════════════════════════════════════════════
#  MATERIAL BUILDER
# ═══════════════════════════════════════════════════════════════

def build_principled_material(mat_name, texture_map, has_emission=False):
    """
    Create a Principled BSDF material with linked texture maps.
    Returns the material.
    """
    # Remove existing material if reimporting
    if mat_name in bpy.data.materials:
        old = bpy.data.materials[mat_name]
        bpy.data.materials.remove(old)

    mat = bpy.data.materials.new(name=mat_name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links

    # Clear defaults
    nodes.clear()

    # Output node
    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (800, 0)

    # Principled BSDF
    principled = nodes.new('ShaderNodeBsdfPrincipled')
    principled.location = (400, 0)
    links.new(principled.outputs['BSDF'], output.inputs['Surface'])

    # Enable emission in Principled BSDF (Blender 4.x uses "Emission Color",
    # Blender 3.x uses "Emission")
    use_emission_socket = "Emission Color" in principled.inputs
    emission_socket_name = "Emission Color" if use_emission_socket else "Emission"

    y_offset = 0

    def add_image_node(filepath, label, to_socket, to_input, colorspace=None):
        """Add an image texture node and link it."""
        if not filepath or not os.path.isfile(filepath):
            return None
        node = nodes.new('ShaderNodeTexImage')
        node.location = (-200, y_offset)
        node.label = label
        try:
            img = bpy.data.images.load(filepath)
            if colorspace:
                img.colorspace_settings.name = colorspace
            node.image = img
        except Exception as e:
            log(f"  Warning: Could not load {filepath}: {e}", "WARN")
            return None
        links.new(node.outputs['Color'], to_socket.inputs[to_input])
        return node

    def add_image_node_alpha(filepath, label, to_socket, to_input):
        """Add an image texture node using Alpha output."""
        if not filepath or not os.path.isfile(filepath):
            return None
        node = nodes.new('ShaderNodeTexImage')
        node.location = (-200, y_offset)
        node.label = label
        try:
            img = bpy.data.images.load(filepath)
            node.image = img
        except Exception as e:
            log(f"  Warning: Could not load {filepath}: {e}", "WARN")
            return None
        links.new(node.outputs['Alpha'], to_socket.inputs[to_input])
        return node

    # Diffuse / Albedo → Base Color
    if "diffuse" in texture_map:
        log(f"  Linking Diffuse: {Path(texture_map['diffuse']).name}")
        add_image_node(texture_map["diffuse"], "Diffuse", principled, "Base Color", "sRGB")
        y_offset -= 300

    # Normal Map
    if "normal" in texture_map:
        log(f"  Linking Normal: {Path(texture_map['normal']).name}")
        normal_img_node = nodes.new('ShaderNodeTexImage')
        normal_img_node.location = (-400, y_offset)
        normal_img_node.label = "Normal Map"
        try:
            img = bpy.data.images.load(texture_map["normal"])
            img.colorspace_settings.name = "Non-Color"
            normal_img_node.image = img
        except Exception as e:
            log(f"  Warning: Could not load normal: {e}", "WARN")

        normal_map = nodes.new('ShaderNodeNormalMap')
        normal_map.location = (-100, y_offset)
        normal_map.label = "Normal Map"

        links.new(normal_img_node.outputs['Color'], normal_map.inputs['Color'])
        links.new(normal_map.outputs['Normal'], principled.inputs['Normal'])
        y_offset -= 300

    # Metallic
    if "metallic" in texture_map:
        log(f"  Linking Metallic: {Path(texture_map['metallic']).name}")
        add_image_node(texture_map["metallic"], "Metallic", principled, "Metallic", "Non-Color")
        y_offset -= 300

    # Roughness
    if "roughness" in texture_map:
        log(f"  Linking Roughness: {Path(texture_map['roughness']).name}")
        add_image_node(texture_map["roughness"], "Roughness", principled, "Roughness", "Non-Color")
        y_offset -= 300

    # Emission — OASIS cyberpunk glow
    if "emission" in texture_map:
        log(f"  Linking Emission: {Path(texture_map['emission']).name}")
        add_image_node(texture_map["emission"], "Emission", principled, emission_socket_name, "sRGB")
        principled.inputs['Emission Strength'].default_value = DEFAULT_EMISSION_STRENGTH
        y_offset -= 300
    elif has_emission:
        # No emission texture but flag is set → use base color as emission
        if "diffuse" in texture_map:
            add_image_node(texture_map["diffuse"], "Emission (from Diffuse)",
                           principled, emission_socket_name, "sRGB")
            principled.inputs['Emission Strength'].default_value = DEFAULT_EMISSION_STRENGTH * 0.5

    # Ambient Occlusion (mix into base color)
    if "ao" in texture_map:
        log(f"  Linking AO: {Path(texture_map['ao']).name}")
        # Mix AO with base color using a MixRGB node
        ao_node = nodes.new('ShaderNodeTexImage')
        ao_node.location = (-400, y_offset)
        ao_node.label = "AO"
        try:
            img = bpy.data.images.load(texture_map["ao"])
            img.colorspace_settings.name = "Non-Color"
            ao_node.image = img
        except Exception as e:
            log(f"  Warning: Could not load AO: {e}", "WARN")
            y_offset -= 300
        else:
            mix_ao = nodes.new('ShaderNodeMixRGB')
            mix_ao.location = (-100, y_offset)
            mix_ao.blend_type = 'MULTIPLY'
            mix_ao.inputs['Fac'].default_value = 0.3  # Subtle AO influence
            mix_ao.inputs['Color1'].default_value = (1, 1, 1, 1)
            links.new(ao_node.outputs['Color'], mix_ao.inputs['Color2'])

            # If we have a diffuse, insert AO mix between diffuse and base color
            if "diffuse" in texture_map:
                # Find the diffuse node and re-route through AO mix
                for node in nodes:
                    if node.label == "Diffuse" and node.type == 'TEX_IMAGE':
                        links.new(node.outputs['Color'], mix_ao.inputs['Color1'])
                        links.new(mix_ao.outputs['Color'], principled.inputs['Base Color'])
                        break
            else:
                links.new(mix_ao.outputs['Color'], principled.inputs['Base Color'])

            y_offset -= 300

    # Specular (optional)
    if "specular" in texture_map:
        add_image_node(texture_map["specular"], "Specular", principled, "Specular IOR Level", "Non-Color")
        y_offset -= 300

    # Default values for cyberpunk look
    if "roughness" not in texture_map:
        principled.inputs['Roughness'].default_value = 0.4  # Slightly glossy
    if "metallic" not in texture_map:
        principled.inputs['Metallic'].default_value = 0.0

    return mat


# ═══════════════════════════════════════════════════════════════
#  OBJ IMPORT PIPELINE
# ═══════════════════════════════════════════════════════════════

def import_obj(filepath, collection, character_name):
    """
    Import a single .obj file and process it.
    Returns a list of imported objects.
    """
    log(f"Importing: {filepath}")
    log(f"  Character: {character_name}")

    # Record existing objects to find new ones
    existing_objects = set(bpy.data.objects)

    # Import OBJ
    bpy.ops.import_scene.obj(
        filepath=str(filepath),
        axis_forward='-Z',
        axis_up='Y',
        use_split_objects=True,
        use_split_groups=True,
        use_smooth_groups=True,
        use_uvs=True,
        use_materials=True,
        global_clamp_size=0.0,
    )

    imported_objects = [obj for obj in bpy.data.objects if obj not in existing_objects]

    if not imported_objects:
        log(f"  Warning: No objects imported from {filepath}", "WARN")
        return []

    log(f"  Imported {len(imported_objects)} object(s)")

    # ── Step 1: Rename & organize into collection ──
    root_obj = None
    for obj in imported_objects:
        # The main mesh is usually the largest
        if obj.type == 'MESH':
            if root_obj is None:
                root_obj = obj
            elif len(obj.data.vertices) > len(root_obj.data.vertices):
                root_obj = obj

    if root_obj is None:
        root_obj = imported_objects[0]

    # Rename root
    root_obj.name = character_name
    if root_obj.data:
        root_obj.data.name = f"{character_name}_Mesh"

    # Rename other objects
    for obj in imported_objects:
        if obj != root_obj:
            short_name = Path(filepath).stem
            obj.name = f"{character_name}_{obj.name}"
            if obj.data:
                obj.data.name = f"{character_name}_{obj.data.name}"

    # Parent everything under root if there are multiple
    for obj in imported_objects:
        link_to_collection(obj, collection)
        if obj != root_obj:
            obj.parent = root_obj
            # Keep the transform
            obj.matrix_parent_inverse = root_obj.matrix_world.inverted()

    # ── Step 2: Fix orientation (Y-up → Z-up) ──
    if FIX_ORIENTATION:
        # OBJ files typically export with Z-up, Blender expects Z-up too,
        # but some OBJ exporters use Y-up. We detect by checking if the
        # model is lying on its side (height < width significantly).
        dims = get_mesh_bounds(root_obj)
        max_dim = max(dims.x, dims.y, dims.z)

        # If Y is the dominant axis (height), it's Y-up → needs rotation
        if dims.y > dims.z * 1.3 and dims.y > dims.x * 1.3:
            log(f"  Detected Y-up orientation, applying -90° X rotation")
            rot_mat = Matrix.Rotation(-math.pi / 2, 4, 'X')
            for obj in imported_objects:
                if obj.type == 'MESH':
                    obj.data.transform(rot_mat)
        # If X is the dominant axis, might be sideways
        elif dims.x > dims.z * 1.3 and dims.x > dims.y * 1.3:
            log(f"  Detected X-up orientation, applying 90° Z rotation")
            rot_mat = Matrix.Rotation(math.pi / 2, 4, 'Z')
            for obj in imported_objects:
                if obj.type == 'MESH':
                    obj.data.transform(rot_mat)

    # ── Step 3: Scale to target height ──
    scale = compute_scale_factor(root_obj)
    log(f"  Scale factor: {scale:.4f} (to reach {TARGET_HEIGHT_M}m)")

    for obj in imported_objects:
        if obj.type == 'MESH':
            # Apply scale to mesh data directly
            scale_mat = Matrix.Scale(scale, 4)
            obj.data.transform(scale_mat)

    # Verify final height
    final_dims = get_mesh_bounds(root_obj)
    final_height = max(final_dims.x, final_dims.y, final_dims.z)
    log(f"  Final height: {final_height:.3f}m")

    # ── Step 4: Detect and link textures ──
    obj_dir = Path(filepath).parent
    texture_map = scan_for_textures(str(obj_dir))

    # Also check for per-material textures (e.g., materialname_diffuse.png)
    if root_obj.type == 'MESH' and root_obj.data.materials:
        for mat_slot in root_obj.materials:
            if mat_slot and mat_slot.use_nodes:
                # Check if this material already has textures
                has_any_tex = False
                for node in mat_slot.node_tree.nodes:
                    if node.type == 'TEX_IMAGE' and node.image:
                        has_any_tex = True
                        break
                if not has_any_tex:
                    # Try to find material-specific textures
                    mat_name_lower = mat_slot.name.lower()
                    mat_tex_dir = obj_dir / "textures"
                    if not mat_tex_dir.is_dir():
                        mat_tex_dir = obj_dir
                    mat_textures = scan_for_textures(str(mat_tex_dir))
                    # Prefix-match material name
                    for tex_type, tex_path in mat_textures.items():
                        tex_name = Path(tex_path).stem.lower()
                        if mat_name_lower in tex_name or tex_name.startswith(mat_name_lower[:4]):
                            log(f"  Material '{mat_slot.name}' → {tex_type}: {Path(tex_path).name}")
                            # Link this texture to the material
                            for node in mat_slot.node_tree.nodes:
                                if node.type == 'BSDF_PRINCIPLED':
                                    img_node = mat_slot.node_tree.nodes.new('ShaderNodeTexImage')
                                    img_node.image = bpy.data.images.load(tex_path)
                                    if tex_type in ('normal', 'metallic', 'roughness', 'ao'):
                                        img_node.image.colorspace_settings.name = 'Non-Color'
                                    input_name = {
                                        'diffuse': 'Base Color',
                                        'normal': 'Normal',
                                        'metallic': 'Metallic',
                                        'roughness': 'Roughness',
                                    }.get(tex_type)
                                    if input_name and input_name in node.inputs:
                                        mat_slot.node_tree.links.new(
                                            img_node.outputs['Color'], node.inputs[input_name]
                                        )
                                    break

    # Check for emission indicators in file/directory name
    has_emission = any(kw in str(filepath).lower() for kw in ['glow', 'neon', 'cyber', 'emission'])

    # Build material for each material slot
    if root_obj.type == 'MESH':
        for mat_slot in root_obj.materials:
            if mat_slot is None:
                continue
            # Check if already has proper Principled BSDF
            has_principled = False
            if mat_slot.use_nodes:
                for node in mat_slot.node_tree.nodes:
                    if node.type == 'BSDF_PRINCIPLED':
                        has_principled = True
                        # Check if it already has a texture
                        for link in mat_slot.node_tree.links:
                            if link.to_node == node and link.from_node.type == 'TEX_IMAGE':
                                has_principled = True
                                break
                        break

            if not has_principled or not texture_map:
                # Create a new principled material with our textures
                new_mat = build_principled_material(
                    f"OASIS_{character_name}_{mat_slot.name}",
                    texture_map,
                    has_emission=has_emission
                )
                # Replace the material
                idx = root_obj.materials.find(mat_slot.name)
                root_obj.materials[idx] = new_mat

                # Also apply to child objects that share the same material
                for child in imported_objects:
                    if child != root_obj and child.type == 'MESH':
                        for i, cm in enumerate(child.materials):
                            if cm and cm.name == mat_slot.name:
                                child.materials[i] = new_mat

    # ── Step 5: Set up for OASIS rendering ──
    # Ensure smooth shading
    for obj in imported_objects:
        if obj.type == 'MESH':
            # Set smooth shading
            for poly in obj.data.polygons:
                poly.use_smooth = True
            # Enable auto-smooth normals (Blender 3.x)
            if hasattr(obj.data, 'use_auto_smooth'):
                obj.data.use_auto_smooth = True
                obj.data.auto_smooth_angle = math.radians(30)

    # Set origin to base of model
    for obj in imported_objects:
        if obj.type == 'MESH':
            bpy.context.view_layer.objects.active = obj
            obj.select_set(True)
            try:
                bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')
            except:
                pass  # May fail in some contexts
            obj.select_set(False)

    log(f"  ✓ Done: {character_name} ({len(imported_objects)} meshes, {len(texture_map)} textures)")
    return imported_objects


# ═══════════════════════════════════════════════════════════════
#  BATCH PIPELINE
# ═══════════════════════════════════════════════════════════════

def run_batch_import():
    """Main entry point: scan directories and import all OBJ files."""
    asset_dir = Path(ASSET_DIR)

    if not asset_dir.is_dir():
        log(f"ERROR: Asset directory not found: {ASSET_DIR}", "ERROR")
        log("Please edit the ASSET_DIR variable at the top of this script.", "ERROR")
        return

    log("=" * 60)
    log("OASIS Avatar Pipeline — Batch OBJ Import")
    log("=" * 60)
    log(f"Asset directory: {ASSET_DIR}")
    log(f"Target height: {TARGET_HEIGHT_M}m")
    log(f"Orientation fix: {'ON' if FIX_ORIENTATION else 'OFF'}")
    log(f"Emission strength: {DEFAULT_EMISSION_STRENGTH}")
    log("")

    # Create master collection
    master = get_or_create_collection(MASTER_COLLECTION)

    # Scan for OBJ files
    obj_files = sorted(asset_dir.rglob("*.obj"))

    if not obj_files:
        log("No .obj files found. Checking common variations...", "WARN")
        # Try .OBJ too
        obj_files = sorted(asset_dir.rglob("*.OBJ"))
        if not obj_files:
            log(f"No OBJ files found in {ASSET_DIR}", "ERROR")
            log("Supported extensions: .obj, .OBJ", "ERROR")
            return

    log(f"Found {len(obj_files)} OBJ file(s)")
    log("")

    # Group by parent directory (character folder)
    characters = {}
    for obj_file in obj_files:
        # Use parent directory as the character group
        parent_name = obj_file.parent.relative_to(asset_dir)
        if str(parent_name) == '.':
            parent_name = obj_file.stem
        else:
            parent_name = str(parent_name).split(os.sep)[0]

        if parent_name not in characters:
            characters[parent_name] = []
        characters[parent_name].append(obj_file)

    total_imported = 0
    total_failed = 0

    for char_group, files in sorted(characters.items()):
        log(f"── Character Group: {char_group} ──")

        # Create collection for this character group
        char_coll = get_or_create_collection(char_group, parent=master)

        for obj_file in files:
            character_name = resolve_character_name(obj_file)

            # Create sub-collection for this specific mesh
            mesh_coll_name = character_name
            if len(files) > 1:
                # If multiple meshes per character, use LOD or variant naming
                variant = obj_file.stem.lower()
                if 'lod' in variant or 'low' in variant:
                    mesh_coll_name = f"{character_name}_LOD"
                elif 'high' in variant:
                    mesh_coll_name = f"{character_name}_High"
                else:
                    mesh_coll_name = f"{character_name}_{obj_file.stem}"

            mesh_coll = get_or_create_collection(mesh_coll_name, parent=char_coll)

            try:
                imported = import_obj(obj_file, mesh_coll, character_name)
                total_imported += len(imported)
            except Exception as e:
                log(f"  FAILED: {e}", "ERROR")
                total_failed += 1

        log("")

    # ── Summary ──
    log("=" * 60)
    log("IMPORT COMPLETE")
    log(f"  Characters: {len(characters)}")
    log(f"  Objects imported: {total_imported}")
    log(f"  Failures: {total_failed}")
    log("=" * 60)

    # List collection hierarchy
    log("")
    log("Collection Hierarchy:")
    def print_coll(coll, indent=0):
        log(f"{'  ' * indent}📁 {coll.name}")
        for obj in coll.objects:
            log(f"{'  ' * indent}  🎭 {obj.name} ({obj.type})")
        for child in coll.children:
            print_coll(child, indent + 1)
    print_coll(master)


# ═══════════════════════════════════════════════════════════════
#  RUN
# ═══════════════════════════════════════════════════════════════

if __name__ == "__main__":
    run_batch_import()
else:
    # If run from Blender's text editor, also execute
    run_batch_import()
