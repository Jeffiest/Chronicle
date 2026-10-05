#pragma once

struct Draw3DVisual;

// Mod framework phase 1b (Windows fork): model dump and replacement. See modmodel.cpp.
// Called once for each model the game builds, after its geometry exists and before it is uploaded.
// `name` is the first texture the model uses (the same name the texture mods use). `vertex`/`vertex_count` are the model's
// original vertex positions as built (the bind pose), kept so a replacement can follow the original's animation.
void ModsModelHook(Draw3DVisual &visual, const char *name, const float (*vertex)[4], int vertex_count, const void *block);
// Called when the game has re-skinned the original model's vertices in place (its "remake"): moves the replacement's
// vertices by the nearby original vertices' displacement so a replaced character or enemy animates.
void ModsModelSkin(Draw3DVisual &visual, const float (*vertex)[4], const void *block);
// From the game's CPU skinning (MotionProc2): Begin as it starts a model's pose, Vertex for each (vertex, bone weight) it skins with
// the bone matrices given. Replacement models copy the weights of their nearest original vertex and are posed with the same math.
void ModsSkinBegin(const float (*mdt_vertices)[4]);
void ModsSkinVertex(const float (*mdt_vertices)[4], unsigned vertex, const float base[4][4], const float bone[4][4],
                    const float inverse[4][4], float weight);
