//=============================================================================//
//
// Purpose: BSP collision debug rendering
//
// $NoKeywords: $
//=============================================================================//
#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "tier1/cmd.h"
#include "engine/cmodel_bsp_debug.h"

#ifndef DEDICATED
#include "tier0/memstd.h"
#include "mathlib/mathlib.h"
#include "tier2/renderutils.h"
#include "engine/debugoverlay.h"
#include "engine/client/clientstate.h"
#include "engine/client/vengineclient_impl.h"
#include "game/client/cliententitylist.h"
#endif // !DEDICATED

//-----------------------------------------------------------------------------
// ConVars - defined outside DEDICATED block so they're available on server too
//-----------------------------------------------------------------------------
ConVar bsp_collision_debug("bsp_collision_debug", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT, 
	"Enable BSP collision debug rendering (draws collision triangles)");

ConVar bsp_collision_debug_mode("bsp_collision_debug_mode", "3", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT,
	"Debug draw mode: 1=wireframe, 2=solid, 3=both");

ConVar bsp_collision_debug_radius("bsp_collision_debug_radius", "2048", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT,
	"Radius around player to render collision triangles", true, 64.f, true, 16384.f);

ConVar bsp_collision_debug_alpha("bsp_collision_debug_alpha", "32", FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT,
	"Alpha value for solid debug rendering", true, 0.f, true, 255.f);

#ifndef DEDICATED
//-----------------------------------------------------------------------------
// Triangle colors for per-triangle visualization (more vibrant)
//-----------------------------------------------------------------------------
static const Color s_TriangleColors[] = {
	Color(255, 50, 50, 255),    // Bright red
	Color(255, 150, 50, 255),   // Orange
	Color(255, 220, 50, 255),   // Gold
	Color(180, 255, 50, 255),   // Lime
	Color(50, 255, 100, 255),   // Green
	Color(50, 255, 200, 255),   // Teal
	Color(50, 220, 255, 255),   // Cyan
	Color(50, 150, 255, 255),   // Sky blue
	Color(80, 80, 255, 255),    // Blue
	Color(150, 50, 255, 255),   // Purple
	Color(220, 50, 255, 255),   // Magenta
	Color(255, 50, 180, 255),   // Pink
	Color(255, 100, 100, 255),  // Light red
	Color(100, 255, 150, 255),  // Mint
	Color(150, 200, 255, 255),  // Light blue
	Color(255, 180, 100, 255),  // Peach
};

//-----------------------------------------------------------------------------
// Purpose: Get a unique color for a triangle based on hash
//-----------------------------------------------------------------------------
static Color GetTriangleColor(uint32_t leafIdx, int triIdx, int alpha)
{
	// Simple hash to get varied colors per triangle
	uint32_t hash = leafIdx * 31 + triIdx * 17;
	const int numColors = V_ARRAYSIZE(s_TriangleColors);
	Color c = s_TriangleColors[hash % numColors];
	c[3] = (unsigned char)alpha;
	return c;
}

//-----------------------------------------------------------------------------
// Purpose: Initialize the BSP collision debug system
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::Init()
{
	// Nothing to do here yet
}

//-----------------------------------------------------------------------------
// Purpose: Shutdown the BSP collision debug system
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::Shutdown()
{
	// Nothing to do here
}

//-----------------------------------------------------------------------------
// Purpose: Decode a packed int16 vertex from the vertex buffer
// Packed format: 6 bytes (int16 x, int16 y, int16 z)
// Decode: world = origin + int16 * scale (where scale already includes *65536)
// The SIMD code does: cvtepi32_ps(unpacklo_epi16(0, int16)) * scale + origin
// unpacklo_epi16(0, int16) puts int16 in high 16 bits = int16 << 16
// So effective decode is: origin + (int16 << 16) * scale = origin + int16 * 65536 * scale
//-----------------------------------------------------------------------------
Vector3D CBSPCollisionDebug::DecodePackedVertex(const int16_t* packedVerts, int vertexIndex,
	const Vector3D& origin, float quantScale)
{
	const int16_t* v = &packedVerts[vertexIndex * 3];
	const float effectiveScale = quantScale * 65536.0f;
	
	return Vector3D(
		origin.x + (float)v[0] * effectiveScale,
		origin.y + (float)v[1] * effectiveScale,
		origin.z + (float)v[2] * effectiveScale
	);
}

//-----------------------------------------------------------------------------
// Purpose: Decode a float vertex from the vertex buffer
// Float format: 12 bytes (float x, float y, float z)
// Type 4 float vertices are ABSOLUTE world coordinates, NOT relative to origin!
// This differs from Type 5 packed vertices which are origin-relative.
//-----------------------------------------------------------------------------
Vector3D CBSPCollisionDebug::DecodeFloatVertex(const float* floatVerts, int vertexIndex,
	const Vector3D& origin)
{
	// Float vertices are stored as absolute world coordinates
	// The origin parameter is not used for Type 4 (kept for API compatibility)
	const float* v = &floatVerts[vertexIndex * 3];
	return Vector3D(v[0], v[1], v[2]);
}

//-----------------------------------------------------------------------------
// Purpose: Draw a triangle with the specified render mode
// Mode 1 = wireframe, Mode 2 = solid, Mode 3 = both
// Uses IgnoreZ materials (bZBuffer=false) to render through geometry and avoid Z-fighting
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawTriangle(const Vector3D& v0, const Vector3D& v1, const Vector3D& v2, const Color& color, int renderMode)
{
	if (renderMode == 1 || renderMode == 3)
	{
		// Wireframe edges - use brighter color for better visibility
		Color wireColor = color;
		wireColor[3] = 255; // Full alpha for wireframe
		RenderLine(v0, v1, wireColor, false);  // false = IgnoreZ, renders through geometry
		RenderLine(v1, v2, wireColor, false);
		RenderLine(v2, v0, wireColor, false);
	}
	if (renderMode == 2 || renderMode == 3)
	{
		// Solid fill - also use IgnoreZ to avoid Z-fighting
		RenderTriangle(v0, v1, v2, color, false);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw leaf geometry (actual triangles) for a leaf node
// Child types from remap tool:
//   0 = Internal node
//   1 = None/empty
//   2 = Empty leaf
//   3 = Bundle (references other nodes)
//   4 = TriStrip (float vertices)
//   5 = Poly3 (packed int16, triangle)
//   6 = Poly4 (packed int16, quad - 2 triangles)
//   7 = Poly5+ (packed int16, 5+ vertices)
//   8 = ConvexHull
//   9 = StaticProp
//   10 = Heightfield
//
// Leaf data format for Poly3 (Type 5):
//   Header (4 bytes): bits 0-11 = surfPropIdx, bits 12-15 = (numPolys-1), bits 16-31 = baseVertex
//   Per-triangle (4 bytes): bits 0-10 = v0_offset, bits 11-19 = v1_delta, bits 20-28 = v2_delta
//   Vertex indexing: running_base = baseVertex << 10, v0 = running_base + offset
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawLeafTriangles(const CollisionModelContext_t* ctx, uint32_t childIdx,
	int childType, const Vector3D& origin, float quantScale, int depth)
{
	if (!ctx || childIdx == 0 || !ctx->verts || !ctx->leafDataStream)
	{
		return;
	}

	// Cast verts pointer appropriately based on type
	// Type 4 uses float vertices (12 bytes per vertex)
	// Type 5/6/7 use packed int16 vertices (6 bytes per vertex)

	const int renderMode = bsp_collision_debug_mode.GetInt();
	const int alpha = bsp_collision_debug_alpha.GetInt();

	//if (childType == 5)
	//{
	//	// Poly3 - packed int16 triangle
	//	// childIdx is the offset into leafDataStream (in uint32_t units)
	//	const uint32_t* leafData = &ctx->leafDataStream[childIdx];
	//	const int16_t* packedVerts = reinterpret_cast<const int16_t*>(ctx->verts);
	//	
	//	// Header format: bits 0-11 = surfPropIdx, bits 12-15 = numPolys-1, bits 16-31 = baseVertex
	//	const uint32_t header = leafData[0];
	//	const int numPolys = ((header >> 12) & 0xF) + 1;
	//	const int baseVertex = (header >> 16) & 0xFFFF;
	//	
	//	int runningBase = baseVertex << 10;  // Vertex base index
	//	
	//	for (int i = 0; i < numPolys && i < 16; i++)
	//	{
	//		// Per-triangle data: bits 0-10 = v0_offset, bits 11-19 = v1_delta, bits 20-28 = v2_delta
	//		const uint32_t triData = leafData[1 + i];
	//		const int v0_offset = triData & 0x7FF;         // 11 bits
	//		const int v1_delta = (triData >> 11) & 0x1FF;  // 9 bits
	//		const int v2_delta = (triData >> 20) & 0x1FF;  // 9 bits
	//		
	//		// Calculate absolute vertex indices
	//		const int idx0 = runningBase + v0_offset;
	//		const int idx1 = idx0 + 1 + v1_delta;
	//		const int idx2 = idx0 + 1 + v2_delta;
	//		
	//		// Update running base for next triangle
	//		runningBase = idx0;
	//		
	//		// Decode vertices and draw triangle
	//		const Vector3D v0 = DecodePackedVertex(packedVerts, idx0, origin, quantScale);
	//		const Vector3D v1 = DecodePackedVertex(packedVerts, idx1, origin, quantScale);
	//		const Vector3D v2 = DecodePackedVertex(packedVerts, idx2, origin, quantScale);
	//		
	//		// Per-triangle color for variety
	//		const Color triColor = GetTriangleColor(childIdx, i, alpha);
	//		DrawTriangle(v0, v1, v2, triColor, renderMode);
	//	}
	//}
	//else if (childType == 6)
	//{
	//	// Poly4 - packed int16 quad (draw as 2 triangles)
	//	const uint32_t* leafData = &ctx->leafDataStream[childIdx];
	//	const int16_t* packedVerts = reinterpret_cast<const int16_t*>(ctx->verts);
	//	
	//	const uint32_t header = leafData[0];
	//	const int numPolys = ((header >> 12) & 0xF) + 1;
	//	const int baseVertex = (header >> 16) & 0xFFFF;
	//	
	//	int runningBase = baseVertex << 10;
	//	
	//	for (int i = 0; i < numPolys && i < 16; i++)
	//	{
	//		// For quads, we need 4 vertex references
	//		const uint32_t triData = leafData[1 + i];
	//		const int v0_offset = triData & 0x7FF;
	//		const int v1_delta = (triData >> 11) & 0x1FF;
	//		const int v2_delta = (triData >> 20) & 0x1FF;
	//		
	//		const int idx0 = runningBase + v0_offset;
	//		const int idx1 = idx0 + 1 + v1_delta;
	//		const int idx2 = idx0 + 1 + v2_delta;
	//		const int idx3 = idx2 + 1;  // Assume v3 follows v2
	//		
	//		runningBase = idx0;
	//		
	//		const Vector3D v0 = DecodePackedVertex(packedVerts, idx0, origin, quantScale);
	//		const Vector3D v1 = DecodePackedVertex(packedVerts, idx1, origin, quantScale);
	//		const Vector3D v2 = DecodePackedVertex(packedVerts, idx2, origin, quantScale);
	//		const Vector3D v3 = DecodePackedVertex(packedVerts, idx3, origin, quantScale);
	//		
	//		// Per-triangle color for variety
	//		const Color triColor = GetTriangleColor(childIdx, i * 2, alpha);
	//		const Color triColor2 = GetTriangleColor(childIdx, i * 2 + 1, alpha);
	//		
	//		// Draw as two triangles
	//		DrawTriangle(v0, v1, v2, triColor, renderMode);
	//		DrawTriangle(v0, v2, v3, triColor2, renderMode);
	//	}
	//}
	//else if (childType == 7)
	//{
	//	// Poly5+ - packed int16 polygon with 5+ vertices
	//	const uint32_t* leafData = &ctx->leafDataStream[childIdx];
	//	const int16_t* packedVerts = reinterpret_cast<const int16_t*>(ctx->verts);
	//	
	//	const uint32_t header = leafData[0];
	//	const int numPolys = ((header >> 12) & 0xF) + 1;
	//	const int baseVertex = (header >> 16) & 0xFFFF;
	//	
	//	int runningBase = baseVertex << 10;
	//	
	//	for (int i = 0; i < numPolys && i < 16; i++)
	//	{
	//		// Similar to Poly3/4 but with more vertices per polygon
	//		const uint32_t triData = leafData[1 + i];
	//		const int v0_offset = triData & 0x7FF;
	//		const int v1_delta = (triData >> 11) & 0x1FF;
	//		const int v2_delta = (triData >> 20) & 0x1FF;
	//		
	//		const int idx0 = runningBase + v0_offset;
	//		const int idx1 = idx0 + 1 + v1_delta;
	//		const int idx2 = idx0 + 1 + v2_delta;
	//		
	//		runningBase = idx0;
	//		
	//		// For Poly5+, draw what we can as triangles
	//		const Vector3D v0 = DecodePackedVertex(packedVerts, idx0, origin, quantScale);
	//		const Vector3D v1 = DecodePackedVertex(packedVerts, idx1, origin, quantScale);
	//		const Vector3D v2 = DecodePackedVertex(packedVerts, idx2, origin, quantScale);
	//		
	//		// Per-triangle color for variety
	//		const Color triColor = GetTriangleColor(childIdx, i, alpha);
	//		DrawTriangle(v0, v1, v2, triColor, renderMode);
	//	}
	//}
	if (childType == 4)
	{
		// Type 4 - float vertices (triangle format, not strip)
		// Uses same leaf data format as Type 5 but with float vertices
		// Float vertices are ABSOLUTE world coordinates (not origin-relative)
		const uint32_t* leafData = &ctx->leafDataStream[childIdx];
		const float* floatVerts = reinterpret_cast<const float*>(ctx->verts);
		
		const uint32_t header = leafData[0];
		const int numPolys = ((header >> 12) & 0xF) + 1;
		const int baseVertex = (header >> 16) & 0xFFFF;
		
		int runningBase = baseVertex << 10;
		
		for (int i = 0; i < numPolys && i < 16; i++)
		{
			const uint32_t triData = leafData[1 + i];
			const int v0_offset = triData & 0x7FF;
			const int v1_delta = (triData >> 11) & 0x1FF;
			const int v2_delta = (triData >> 20) & 0x1FF;
			
			const int idx0 = runningBase + v0_offset;
			const int idx1 = idx0 + 1 + v1_delta;
			const int idx2 = idx0 + 1 + v2_delta;
			
			runningBase = idx0;
			
			const Vector3D v0 = DecodeFloatVertex(floatVerts, idx0, origin);
			const Vector3D v1 = DecodeFloatVertex(floatVerts, idx1, origin);
			const Vector3D v2 = DecodeFloatVertex(floatVerts, idx2, origin);
			
			// Per-triangle color for variety
			const Color triColor = GetTriangleColor(childIdx, i, alpha);
			DrawTriangle(v0, v1, v2, triColor, renderMode);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Get the bounds of a BVH node child
// The bounds in BVH nodes are stored as int16 quantized values
// The engine uses SIMD unpacking that shifts int16 to high 16 bits of int32,
// effectively multiplying by 65536, then multiplies by decodeScale and adds origin.
// Formula: world = int16_value * 65536 * decodeScale + origin
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::GetNodeBounds(const CollBvh4Node_t* node, int childIndex, float scale,
	const Vector3D& origin, Vector3D& outMins, Vector3D& outMaxs)
{
	// The engine's SIMD code does: unpacklo_epi16(0, int16) which shifts to high 16 bits
	// This effectively multiplies by 65536, then multiplies by decodeScale
	// So the effective scale is: decodeScale * 65536
	const float effectiveScale = scale * 65536.0f;

	// BVH nodes store bounds as int16 values per axis:
	// minMax[axis][0][child] = min
	// minMax[axis][1][child] = max
	// world = int16_value * effectiveScale + origin
	outMins.x = (float)node->GetMin(0, childIndex) * effectiveScale + origin.x;
	outMins.y = (float)node->GetMin(1, childIndex) * effectiveScale + origin.y;
	outMins.z = (float)node->GetMin(2, childIndex) * effectiveScale + origin.z;
	
	outMaxs.x = (float)node->GetMax(0, childIndex) * effectiveScale + origin.x;
	outMaxs.y = (float)node->GetMax(1, childIndex) * effectiveScale + origin.y;
	outMaxs.z = (float)node->GetMax(2, childIndex) * effectiveScale + origin.z;
}

//-----------------------------------------------------------------------------
// Purpose: Check if a node child intersects with an AABB
//-----------------------------------------------------------------------------
bool CBSPCollisionDebug::NodeIntersectsAABB(const CollBvh4Node_t* node, int childIndex, float scale,
	const Vector3D& filterMins, const Vector3D& filterMaxs)
{
	// Get origin from the current collision context being rendered
	Vector3D origin(0, 0, 0);
	if (g_ppCollisionModelContexts && *g_ppCollisionModelContexts)
	{
		const CollisionModelContext_t* ctx = *g_ppCollisionModelContexts;
		origin.x = ctx->scaleOriginX;
		origin.y = ctx->scaleOriginY;
		origin.z = ctx->scaleOriginZ;
	}

	Vector3D nodeMins, nodeMaxs;
	GetNodeBounds(node, childIndex, scale, origin, nodeMins, nodeMaxs);

	// AABB intersection test
	if (nodeMins.x > filterMaxs.x || nodeMaxs.x < filterMins.x)
		return false;
	if (nodeMins.y > filterMaxs.y || nodeMaxs.y < filterMins.y)
		return false;
	if (nodeMins.z > filterMaxs.z || nodeMaxs.z < filterMins.z)
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Recursively draw BVH nodes
// Based on CollBvh_VisitNodes_r from engine decompilation
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawNodeRecursive(const CollBvh4Node_t* nodes, int nodeIndex,
	const Vector3D& origin, float scale, int depth, int maxDepth,
	const Vector3D& filterMins, const Vector3D& filterMaxs)
{
	if (maxDepth >= 0 && depth > maxDepth)
		return;

	if (nodeIndex < 0)
		return;

	const CollBvh4Node_t* node = &nodes[nodeIndex];

	// Child type extraction (from packedMetaData[2] and [3]):
	//   type0 = packedMetaData[2] & 0xF
	//   type1 = (packedMetaData[2] >> 4) & 0xF
	//   type2 = packedMetaData[3] & 0xF
	//   type3 = (LOBYTE(packedMetaData[3]) >> 4)
	//
	// Child index extraction:
	//   index[i] = packedMetaData[i] >> 8
	//
	// Type meanings:
	//   0 = internal node (recurse with childIndex)
	//   1 = empty/skip
	//   2+ = leaf with polygon data

	for (int i = 0; i < 4; i++)
	{
		const int childType = node->GetChildType(i);
		const uint32_t childIdx = node->GetChildIndex(i);

		// Type 0 = internal node, Type >= 1 = leaf (or empty if type==1)
		const bool isInternalNode = (childType == 0);
		const bool isLeaf = (childType >= 2);  // type 1 is empty/skip, type 2+ is leaf

		// Skip empty children (type 1 or internal with index 0)
		if (childType == 1)
			continue;
		if (childIdx == 0 && isInternalNode)
			continue;

		// Check if this child intersects our filter AABB
		if (!NodeIntersectsAABB(node, i, scale, filterMins, filterMaxs))
			continue;

		// For leaves, draw the actual collision triangles
		if (isLeaf && childIdx > 0)
		{
			const CollisionModelContext_t* ctx = *g_ppCollisionModelContexts;
			
			// Only draw types 4-7 which have triangle data
			if (childType >= 4 && childType <= 7)
			{
				DrawLeafTriangles(ctx, childIdx, childType, origin, scale, depth);
			}
		}
		else if (isInternalNode)
		{
			// Recurse into internal nodes
			if (childIdx > 0)
			{
				DrawNodeRecursive(nodes, childIdx, origin, scale, depth + 1, maxDepth, filterMins, filterMaxs);
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draw BVH nodes around a specific position
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::DrawBVHNodesAroundPoint(const Vector3D& pos, float radius, int maxDepth)
{
	// Use model index 0 for the world BSP
	if (!g_ppCollisionModelContexts || !*g_ppCollisionModelContexts)
		return;

	const CollisionModelContext_t* ctx = *g_ppCollisionModelContexts;
	
	if (!ctx->bvhNodes)
		return;

	// Get scale from the collision context - the BVH uses int16 bounds that need to be scaled
	float scale = ctx->quantScale;
	if (scale <= 0.0f)
		scale = 1.0f;

	const Vector3D origin(ctx->scaleOriginX, ctx->scaleOriginY, ctx->scaleOriginZ);
	const Vector3D filterMins = pos - Vector3D(radius, radius, radius);
	const Vector3D filterMaxs = pos + Vector3D(radius, radius, radius);

	// Start from root node (index 0)
	DrawNodeRecursive(ctx->bvhNodes, 0, origin, scale, 0, maxDepth, filterMins, filterMaxs);
}

//-----------------------------------------------------------------------------
// Purpose: Main render entry point
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::Render()
{
	const int debugMode = bsp_collision_debug.GetInt();
	if (debugMode <= 0)
		return;

	if (!g_pClientState || !g_pClientState->IsActive())
	{
		static bool sWarnedClientState = false;
		if (!sWarnedClientState)
		{
			Warning(eDLL_T::ENGINE, "BSP Collision Debug: Client not active\n");
			sWarnedClientState = true;
		}
		return;
	}

	if (!g_ppCollisionModelContexts)
	{
		// Pattern not found - feature disabled for this game version
		return;
	}

	if (!*g_ppCollisionModelContexts)
	{
		// Not loaded yet - this is normal before map load
		return;
	}

	const CollisionModelContext_t* ctx = *g_ppCollisionModelContexts;
	if (!ctx || !ctx->bvhNodes)
		return;

	// Get player position for filtering
	Vector3D playerPos(0, 0, 0);

	// Try to get the local player's position
	if (g_pEngineClient && g_pClientEntityList)
	{
		const int localPlayerIndex = g_pEngineClient->GetLocalPlayer();
		if (localPlayerIndex > 0)
		{
			const IClientEntity* pLocalPlayer = g_pClientEntityList->GetClientEntity(localPlayerIndex);
			if (pLocalPlayer)
			{
				playerPos = pLocalPlayer->GetAbsOrigin();
			}
		}
	}

	const float radius = bsp_collision_debug_radius.GetFloat();

	DrawBVHNodesAroundPoint(playerPos, radius, -1);
}

//-----------------------------------------------------------------------------
// Purpose: Console command to toggle BSP collision debug
//-----------------------------------------------------------------------------
void CBSPCollisionDebug::CC_DrawBSPCollision(const CCommand& args)
{
	if (args.ArgC() < 2)
	{
		Msg(eDLL_T::ENGINE, "Usage: bsp_collision_debug <0/1>\n");
		Msg(eDLL_T::ENGINE, "  0 = off\n");
		Msg(eDLL_T::ENGINE, "  1 = draw collision triangles\n");
		return;
	}

	const int mode = atoi(args.Arg(1));
	bsp_collision_debug.SetValue(mode);
}

#endif // !DEDICATED

//-----------------------------------------------------------------------------
// Signature scanning to find collision data pointer
// Defined outside DEDICATED block so it runs on both client and server
//-----------------------------------------------------------------------------
void VBSPCollisionDebug::GetVar(void) const
{
#ifndef DEDICATED
	// The collision model contexts array is accessed in a function like:
	//   mov eax, edx             ; 8B C2
	//   lea rcx, [rax+rax*8]     ; 48 8D 0C C0
	//   mov rax, cs:qword_XXXX   ; 48 8B 05 XX XX XX XX <-- pointer to collision contexts
	//   lea rax, [rax+rcx*8]     ; 48 8D 04 C8
	//   retn                     ; C3
	//
	// This function returns: qword_1634F1638 + 72 * index
	// The 72-byte CollisionModelContext_t is the per-model collision context struct
	CMemory result = Module_FindPattern(g_GameDll, "8B C2 48 8D 0C C0 48 8B 05 ?? ?? ?? ?? 48 8D 04 C8 C3");
	
	if (result)
	{
		// The RIP-relative address is at offset 0x6 from pattern start (inside the mov rax instruction)
		// mov rax, [rip+offset] is: 48 8B 05 XX XX XX XX
		// Pattern: 8B C2 48 8D 0C C0 [48 8B 05 XX XX XX XX] 48 8D 04 C8 C3
		// Offset to 48 8B 05 is 6, relative address at 6+3=9, instruction ends at 6+7=13
		g_ppCollisionModelContexts = result.Offset(0x6).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CollisionModelContext_t**>();
	}

	// Debug output
	if (g_ppCollisionModelContexts)
	{
		//DevMsg(eDLL_T::ENGINE, "BSP Collision Debug: g_ppCollisionModelContexts = 0x%p\n", g_ppCollisionModelContexts);
	}
	else
	{
		//Warning(eDLL_T::ENGINE, "BSP Collision Debug: Failed to find g_ppCollisionModelContexts - debug rendering disabled\n");
	}
#endif // !DEDICATED
}

///////////////////////////////////////////////////////////////////////////////
// Register the detour - this must be in the .cpp file, not the header
// Defined outside DEDICATED block so ConVars are registered on both client and server
REGISTER(VBSPCollisionDebug);
