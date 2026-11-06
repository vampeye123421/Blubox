# Importing 3D models

Studio can import `.obj`, `.gltf`, `.glb`, and `.fbx` models from **File > Import 3D model** or **Insert > 3D Model**.

The FBX importer brings in triangle geometry, node hierarchy and transforms, vertex normals, UVs, and base material color/roughness/metalness. External and embedded FBX textures are not loaded. Imported meshes and textures remain local assets, so save the project with **Save** or **Save a local copy...** to include its `.assets` directory.

FBX files are parsed without loading external files and are subject to configured file, memory, geometry, material, and hierarchy limits.
