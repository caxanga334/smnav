//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

// AI Navigation areas
// Author: Michael S. Booth (mike@turtlerockstudios.com), January 2003

#include "nav_area.h"
#include "nav_colors.h"
#include "nav.h"
#include "nav_mesh.h"
#include <util/UtilTrace.h>
#include <util/BasePlayer.h>
#include <util/EntityUtils.h>
#include <gametrace.h>
#include <eiface.h>
#include <convar.h>
#include <iplayerinfo.h>
#include <utlbuffer.h>
#include <ivdebugoverlay.h>
#include <vphysics_interface.h>
#include <shareddefs.h>
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar nav_area_bgcolor;
extern IPlayerInfoManager* playerinfomanager;
extern IVDebugOverlay* debugoverlay;
extern IVEngineServer *engine;

unsigned int CNavLadder::m_nextID = 1;

//--------------------------------------------------------------------------------------------------------------
/**
 * Shift the nav area
 */
void CNavLadder::Shift( const Vector &shift )
{
	m_top += shift;
	m_bottom += shift;
}



 void CNavLadder::CompressIDs( CNavMesh* TheNaVmesh )
{
	m_nextID = 1;

	if ( TheNavMesh )
	{
		for ( int i=0; i<TheNavMesh->GetLadders().Count(); ++i )
		{
			TheNavMesh->GetLadders()[i]->m_id = m_nextID++;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
CNavArea ** CNavLadder::GetConnection( LadderConnectionType dir )
{
	switch ( dir )
	{
	case LADDER_TOP_FORWARD:
		return &m_topForwardArea;
	case LADDER_TOP_LEFT:
		return &m_topLeftArea;
	case LADDER_TOP_RIGHT:
		return &m_topRightArea;
	case LADDER_TOP_BEHIND:
		return &m_topBehindArea;
	case LADDER_BOTTOM:
		return &m_bottomArea;
	}

	return NULL;
}


//--------------------------------------------------------------------------------------------------------------
void CNavLadder::OnSplit( CNavArea *original, CNavArea *alpha, CNavArea *beta )
{
	for ( int con=0; con<NUM_LADDER_CONNECTIONS; ++con )
	{
		CNavArea ** areaConnection = GetConnection( (LadderConnectionType)con );

		if ( areaConnection && *areaConnection == original )
		{
			*areaConnection = alpha->GetDistanceSquaredToPoint( m_top )
					< beta->GetDistanceSquaredToPoint( m_top ) ?
					alpha : beta;
		}
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Connect this ladder to given area
 */
void CNavLadder::ConnectTo( CNavArea *area )
{
	float center = (m_top.z + m_bottom.z) * 0.5f;

	if (area->GetCenter().z > center)
	{
		// connect to top
		NavDirType dir;

		Vector dirVector = area->GetCenter() - m_top;
		if ( fabs( dirVector.x ) > fabs( dirVector.y ) )
		{
			dir = dirVector.x > 0.0f ?
					EAST : WEST;
		}
		else
		{
			dir = dirVector.y > 0.0f ?
					SOUTH : NORTH;
		}

		if ( m_dir == dir )
		{
			Warning("Bots may not be able to find exit path to \"behind areas\" when climbing ladders.\n");
			m_topBehindArea = area;
		}
		else if ( OppositeDirection( m_dir ) == dir )
		{
			m_topForwardArea = area;
		}
		else if ( DirectionLeft( m_dir ) == dir )
		{
			m_topLeftArea = area;
		}
		else
		{
			m_topRightArea = area;
		}
	}
	else
	{
		// connect to bottom
		m_bottomArea = area;
	}
}


//--------------------------------------------------------------------------------------------------------------

CNavLadder::~CNavLadder()
{
	// tell the other areas we are going away
	extern NavAreaVector TheNavAreas;
	FOR_EACH_VEC( TheNavAreas, it )
	{
		TheNavAreas[ it ]->OnDestroyNotify( this );
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * invoked when given area is going away
 */
void CNavLadder::OnDestroyNotify( CNavArea *dead )
{
	Disconnect( dead );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Disconnect this ladder from given area
 */
void CNavLadder::Disconnect( CNavArea *area )
{
	if ( m_topForwardArea == area )
	{
		m_topForwardArea = NULL;
	}
	else if ( m_topLeftArea == area )
	{
		m_topLeftArea = NULL;
	}
	else if ( m_topRightArea == area )
	{
		m_topRightArea = NULL;
	}
	else if ( m_topBehindArea == area )
	{
		m_topBehindArea = NULL;
	}
	else if ( m_bottomArea == area )
	{
		m_bottomArea = NULL;
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * returns true if given area is connected in given direction
 */
bool CNavLadder::IsConnected( const CNavArea *area, LadderDirectionType dir ) const
{
	if ( dir == LADDER_DOWN )
	{
		return area == m_bottomArea;
	}
	else if ( dir == LADDER_UP )
	{
		return ( area == m_topForwardArea ||
			area == m_topLeftArea ||
			area == m_topRightArea ||
			area == m_topBehindArea );
	}
	else
	{
		return ( area == m_bottomArea ||
			area == m_topForwardArea ||
			area == m_topLeftArea ||
			area == m_topRightArea ||
			area == m_topBehindArea );
	}
}

//--------------------------------------------------------------------------------------------------------------
void CNavLadder::SetDir( NavDirType dir )
{
	m_dir = dir;

	m_normal.Init();
	AddDirectionVector( &m_normal, m_dir, 1.0f );	// worst-case, we have the NavDirType as a normal

	Vector from = (m_top + m_bottom) * 0.5f + m_normal * 5.0f;
	Vector to = from - m_normal * 32.0f;

	trace_t result;
#ifdef TERROR
	// TERROR: use the MASK_ZOMBIESOLID_BRUSHONLY contents, since that's what zombies use
	UTIL_TraceLine( from, to, MASK_ZOMBIESOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &result );
#else
	UTIL_TraceLine( from, to, MASK_NPCSOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &result );
#endif
	extern IPhysicsSurfaceProps *physprops;
	if (result.fraction != 1.0f
		&& (physprops->GetSurfaceData( result.surface.surfaceProps )->game.climbable != 0
			|| (result.contents & CONTENTS_LADDER) != 0) )
	{
		m_normal = result.plane.normal;
	}
}

void DrawAbsBoxOverlay(edict_t *edict)
{
	int red = 0;
	int green = 200;

	if (edict)
	{
		// Surrounding boxes are axially aligned, so ignore angles
		Vector vecSurroundMins, vecSurroundMaxs;
		edict->GetCollideable()->WorldSpaceSurroundingBounds( &vecSurroundMins, &vecSurroundMaxs );
		Vector center = 0.5f * (vecSurroundMins + vecSurroundMaxs);
		Vector extents = vecSurroundMaxs - center;
		debugoverlay->AddBoxOverlay(center, -extents, extents, QAngle(0,0,0), red, green, 0, 0 ,0);
	}
}

//--------------------------------------------------------------------------------------------------------------
void CNavLadder::DrawLadder( bool isSelected,  bool isMarked, bool isEdit ) const
{

	edict_t* ent = UTIL_GetListenServerEnt();
	extern IServerGameClients* gameclients;
	if (ent == NULL)
		return;

	Vector eye;
	gameclients->ClientEarPosition(ent, &eye);
	Vector2D eyeDir( eye.x - m_bottom.x, eye.y - m_bottom.y );
	eyeDir.NormalizeInPlace();
	bool isFront = DotProduct2D( eyeDir, GetNormal().AsVector2D() ) > 0;

	if ( isEdit )
	{
		isSelected = isMarked = false;
		isFront = true;
	}

	// Highlight ladder entity ------------------------------------------------
	IServerEntity *ladderEntity = m_ladderEntity.Get();
	if ( ladderEntity )
	{
		DrawAbsBoxOverlay(ladderEntity->GetNetworkable()->GetEdict());
	}

	// Draw 'ladder' lines ----------------------------------------------------
	NavEditColor ladderColor = NavNormalColor;
	if ( isFront )
	{
		if ( isMarked )
		{
			ladderColor = NavMarkedColor;
		}
		else if ( isSelected )
		{
			ladderColor = NavSelectedColor;
		}
		else
		{
			ladderColor = NavSamePlaceColor;
		}
	}
	else if ( isMarked )
	{
		ladderColor = NavMarkedColor;
	}
	else if ( isSelected )
	{
		ladderColor = NavSelectedColor;
	}

	Vector right(0, 0, 0), up( 0, 0, 0 );
	VectorVectors( GetNormal(), right, up );
	if ( up.z <= 0.0f )
	{
		AssertMsg( false, "A nav ladder has an invalid normal" );
		up.Init( 0, 0, 1 );
	}

	right *= m_width * 0.5f;

	Vector bottomLeft = m_bottom - right;
	Vector bottomRight = m_bottom + right;
	Vector topLeft = m_top - right;
	Vector topRight = m_top + right;

	int bgcolor[4];
	if ( 4 == sscanf( nav_area_bgcolor.GetString(), "%d %d %d %d", &(bgcolor[0]), &(bgcolor[1]), &(bgcolor[2]), &(bgcolor[3]) ) )
	{
		for ( int i=0; i<4; ++i )
			bgcolor[i] = clamp( bgcolor[i], 0, 255 );

		if ( bgcolor[3] > 0 )
		{
			Vector offset( 0, 0, 0 );
			AddDirectionVector( &offset, OppositeDirection( m_dir ), 1 );
			debugoverlay->AddTriangleOverlay( topLeft+offset, topRight+offset, bottomRight+offset, bgcolor[0], bgcolor[1], bgcolor[2], bgcolor[3], true, 0.15f );
			debugoverlay->AddTriangleOverlay( bottomRight+offset, bottomLeft+offset, topLeft+offset, bgcolor[0], bgcolor[1], bgcolor[2], bgcolor[3], true, 0.15f );
		}
	}

	NavDrawLine( topLeft, bottomLeft, ladderColor );
	NavDrawLine( topRight, bottomRight, ladderColor );

	while ( bottomRight.z < topRight.z )
	{
		NavDrawLine( bottomRight, bottomLeft, ladderColor );
		bottomRight += up * (GenerationStepSize/2);
		bottomLeft += up * (GenerationStepSize/2);
	}

	// Draw connector lines ---------------------------------------------------
	if ( !isEdit )
	{
		Vector bottom = m_bottom;
		Vector top = m_top;

		NavDrawLine( top, bottom, NavConnectedTwoWaysColor );

		if (m_bottomArea)
		{
			float offset = GenerationStepSize;
			const Vector& areaBottom = m_bottomArea->GetCenter();

			 // don't draw the bottom connection too high if the ladder is very short
			if ( top.z - bottom.z < GenerationStepSize * 1.5f 			 // don't draw the bottom connection too high if the ladder is high above the area
					|| bottom.z - areaBottom.z > GenerationStepSize * 1.5f )
				offset = 0.0f;

			NavDrawLine( bottom + Vector( 0, 0, offset ), areaBottom, ((m_bottomArea->IsConnected( this, LADDER_UP ))?NavConnectedTwoWaysColor:NavConnectedOneWayColor) );
		}

		if (m_topForwardArea)
			NavDrawLine( top, m_topForwardArea->GetCenter(), ((m_topForwardArea->IsConnected( this, LADDER_DOWN ))?NavConnectedTwoWaysColor:NavConnectedOneWayColor) );

		if (m_topLeftArea)
			NavDrawLine( top, m_topLeftArea->GetCenter(), ((m_topLeftArea->IsConnected( this, LADDER_DOWN ))?NavConnectedTwoWaysColor:NavConnectedOneWayColor) );

		if (m_topRightArea)
			NavDrawLine( top, m_topRightArea->GetCenter(), ((m_topRightArea->IsConnected( this, LADDER_DOWN ))?NavConnectedTwoWaysColor:NavConnectedOneWayColor) );

		if (m_topBehindArea)
			NavDrawLine( top, m_topBehindArea->GetCenter(), ((m_topBehindArea->IsConnected( this, LADDER_DOWN ))?NavConnectedTwoWaysColor:NavConnectedOneWayColor) );
	}
}


//--------------------------------------------------------------------------------------------------------------
void CNavLadder::DrawConnectedAreas( bool isEdit )
{
	CUtlVector< CNavArea * > areas;
	if ( m_topForwardArea )
		areas.AddToTail( m_topForwardArea );
	if ( m_topLeftArea )
		areas.AddToTail( m_topLeftArea );
	if ( m_topRightArea )
		areas.AddToTail( m_topRightArea );
	if ( m_topBehindArea )
		areas.AddToTail( m_topBehindArea );
	if ( m_bottomArea )
		areas.AddToTail( m_bottomArea );

	for ( int i=0; i<areas.Count(); ++i )
	{
		CNavArea *adj = areas[i];

		adj->Draw();

		if ( !isEdit )
		{
			adj->DrawHidingSpots();
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
bool CNavLadder::IsUsableByTeam(int teamNumber) const {
	IServerEntity* ent = m_ladderEntity.Get();
	if (ent == nullptr) {
		return true;
	}
	int *ladderTeamNumber = BaseEntity(ent->GetNetworkable()->GetEdict()).getTeam();
	return ladderTeamNumber == nullptr || teamNumber == *ladderTeamNumber
			|| *ladderTeamNumber == TEAM_UNASSIGNED;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * invoked when a game round restarts
 */
void CNavLadder::OnRoundRestart( void )
{
	FindLadderEntity();
}


//--------------------------------------------------------------------------------------------------------------
void CNavLadder::FindLadderEntity( void )
{
	CUtlLinkedList<edict_t*> ladder;
	findEntWithPatternInName("*ladder", ladder);
	m_ladderEntity = ladder.Count() > 0 ? ladder[0]->GetIServerEntity(): nullptr;
	edict_t *nearest = findNearestEntity(ladder,
			(m_top + m_bottom) * 0.5f, HalfHumanWidth);
	if (nearest != nullptr) {
		m_ladderEntity = nearest->GetIServerEntity();
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Save a navigation ladder to the opened binary stream
 */
void CNavLadder::Save( CUtlBuffer &fileBuffer, unsigned int version ) const
{
	// save ID
	fileBuffer.PutUnsignedInt( m_id );

	// save extent of ladder
	fileBuffer.PutFloat( m_width );

	// save top endpoint of ladder
	fileBuffer.PutFloat( m_top.x );
	fileBuffer.PutFloat( m_top.y );
	fileBuffer.PutFloat( m_top.z );

	// save bottom endpoint of ladder
	fileBuffer.PutFloat( m_bottom.x );
	fileBuffer.PutFloat( m_bottom.y );
	fileBuffer.PutFloat( m_bottom.z );

	// save ladder length
	fileBuffer.PutFloat( m_length );

	// save direction
	fileBuffer.PutUnsignedInt( m_dir );

	// save IDs of connecting areas
	fileBuffer.PutUnsignedInt( m_topForwardArea ? m_topForwardArea->GetID() : 0 );
	fileBuffer.PutUnsignedInt( m_topLeftArea ? m_topLeftArea->GetID() : 0 );
	fileBuffer.PutUnsignedInt( m_topRightArea ? m_topRightArea->GetID() : 0 );
	fileBuffer.PutUnsignedInt( m_topBehindArea ? m_topBehindArea->GetID() : 0 );
	fileBuffer.PutUnsignedInt( m_bottomArea ? m_bottomArea->GetID() : 0 );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Load a navigation ladder from the opened binary stream
 */
void CNavLadder::Load( CNavMesh* TheNavMesh, CUtlBuffer &fileBuffer, unsigned int version )
{
	// load ID
	m_id = fileBuffer.GetUnsignedInt();

	// update nextID to avoid collisions
	if (m_id >= m_nextID)
		m_nextID = m_id+1;

	// load extent of ladder
	m_width = fileBuffer.GetFloat();

	// load top endpoint of ladder
	m_top.x = fileBuffer.GetFloat();
	m_top.y = fileBuffer.GetFloat();
	m_top.z = fileBuffer.GetFloat();

	// load bottom endpoint of ladder
	m_bottom.x = fileBuffer.GetFloat();
	m_bottom.y = fileBuffer.GetFloat();
	m_bottom.z = fileBuffer.GetFloat();

	// load ladder length
	m_length = fileBuffer.GetFloat();

	// load direction
	m_dir = (NavDirType)fileBuffer.GetUnsignedInt();
	SetDir( m_dir ); // regenerate the surface normal

	// load dangling status
	if ( version == 6 )
	{
		bool m_isDangling;
		fileBuffer.Get( &m_isDangling, sizeof(m_isDangling) );
	}

	// load IDs of connecting areas
	m_topForwardArea = TheNavMesh->GetNavAreaByID( fileBuffer.GetUnsignedInt() );
	m_topLeftArea = TheNavMesh->GetNavAreaByID( fileBuffer.GetUnsignedInt() );
	m_topRightArea = TheNavMesh->GetNavAreaByID( fileBuffer.GetUnsignedInt() );
	m_topBehindArea = TheNavMesh->GetNavAreaByID( fileBuffer.GetUnsignedInt() );
	m_bottomArea = TheNavMesh->GetNavAreaByID( fileBuffer.GetUnsignedInt() );
	if ( !m_bottomArea )
	{
		DevMsg( "ERROR: Unconnected ladder #%d bottom at ( %g, %g, %g )\n", m_id, m_bottom.x, m_bottom.y, m_bottom.z );
		DevWarning( "nav_unmark; nav_mark ladder %d; nav_warp_to_mark\n", m_id );
	}
	else if (!m_topForwardArea && !m_topLeftArea && !m_topRightArea)	// can't include behind area, since it is not used when going up a ladder
	{
		DevMsg( "ERROR: Unconnected ladder #%d top at ( %g, %g, %g )\n", m_id, m_top.x, m_top.y, m_top.z );
		DevWarning( "nav_unmark; nav_mark ladder %d; nav_warp_to_mark\n", m_id );
	}

	FindLadderEntity();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Functor returns true if ladder is free, or false if someone is on the ladder
 */
class IsLadderFreeFunctor
{
public:
	IsLadderFreeFunctor( const CNavLadder *ladder, const edict_t *ignore )
	{
		m_ladder = ladder;
		m_ignore = ignore;
	}

	bool operator() ( edict_t *ent )
	{
		if (ent == m_ignore)
			return true;
		IPlayerInfo* player = playerinfomanager->GetPlayerInfo(ent);

		if (!BasePlayer(ent).isOnLadder())
			return true;
		// player is on a ladder - is it this one?
		const Vector &feet = player->GetAbsOrigin();
		return feet.z > m_ladder->m_top.z + HalfHumanHeight
				|| feet.z + HumanHeight < m_ladder->m_bottom.z - HalfHumanHeight
				|| Vector2D( m_ladder->m_bottom.x - feet.x, m_ladder->m_bottom.y - feet.y ).IsLengthGreaterThan( 50.0f );
	}

	const CNavLadder *m_ladder;
	const edict_t *m_ignore;
};

//--------------------------------------------------------------------------------------------------------------
/**
 * DEPRECATED: Use CollectPlayers() instead.
 * Iterate over all active players in the game, invoking functor on each.
 * If functor returns false, stop iteration and return false.
 */
template < typename Functor >
bool ForEachPlayer( Functor &func )
{
	for( int i=1; i<=playerinfomanager->GetGlobalVars()->maxClients; ++i )
	{
		edict_t *ent = engine->PEntityOfEntIndex(i);
		IPlayerInfo* player = playerinfomanager->GetPlayerInfo(ent);
		if (player == NULL || (!player->IsPlayer() && !player->IsConnected()) )
			continue;
		if (!func( ent ))
			return false;
	}
	return true;
}



//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if someone is on this ladder
 */
bool CNavLadder::IsInUse( const edict_t *ignore ) const
{
	IsLadderFreeFunctor	isLadderFree( this, ignore );
	return !ForEachPlayer( isLadderFree );
}

//--------------------------------------------------------------------------------------------------------------
Vector CNavLadder::GetPosAtHeight( float height ) const
{
	if ( height < m_bottom.z )
	{
		return m_bottom;
	}

	if ( height > m_top.z || m_top.z == m_bottom.z )
	{
		return m_top;
	}

	float percent = ( height - m_bottom.z ) / ( m_top.z - m_bottom.z );

	return m_top * percent + m_bottom * ( 1.0f - percent );
}

//--------------------------------------------------------------------------------------------------------------
