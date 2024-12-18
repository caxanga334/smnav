#include <extension.h>
#include <manager.h>
#include <util/helpers.h>
#include <util/entprops.h>
#include <util/librandom.h>
#include <mods/tf2/teamfortress2mod.h>
#include <mods/tf2/tf2lib.h>
#include <mods/tf2/nav/tfnavarea.h>
#include <mods/tf2/nav/tfnav_waypoint.h>
#include <entities/tf2/tf_entities.h>
#include "tf2bot.h"

#undef max
#undef min
#undef clamp

CTF2Bot::CTF2Bot(edict_t* edict) : CBaseBot(edict)
{
	m_tf2movement = std::make_unique<CTF2BotMovement>(this);
	m_tf2controller = std::make_unique<CTF2BotPlayerController>(this);
	m_tf2sensor = std::make_unique<CTF2BotSensor>(this);
	m_tf2behavior = std::make_unique<CTF2BotBehavior>(this);
	m_tf2inventory = std::make_unique<CTF2BotInventory>(this);
	m_tf2spymonitor = std::make_unique<CTF2BotSpyMonitor>(this);
	m_desiredclass = TeamFortress2::TFClassType::TFClass_Unknown;
	m_upgrademan.SetMe(this);
	m_sgWaypoint = nullptr;
	m_dispWaypoint = nullptr;
	m_tpentWaypoint = nullptr;
	m_tpextWaypoint = nullptr;
}

CTF2Bot::~CTF2Bot()
{
}

void CTF2Bot::Reset()
{
	CBaseBot::Reset();
}

void CTF2Bot::Update()
{
	CTeamFortress2Mod* mod = CTeamFortress2Mod::GetTF2Mod();

	if (mod->GetCurrentGameMode() == TeamFortress2::GameModeType::GM_MVM)
	{
		if (!m_upgrademan.IsManagerReady())
		{
			m_upgrademan.Update();
		}
	}

	CBaseBot::Update();
}

void CTF2Bot::Frame()
{
	CBaseBot::Frame();
}

void CTF2Bot::TryJoinGame()
{
	JoinTeam();
	auto tfclass = CTeamFortress2Mod::GetTF2Mod()->SelectAClassForBot(this);
	JoinClass(tfclass);
}

void CTF2Bot::Spawn()
{
	FindMyBuildings();

	if (GetMyClassType() > TeamFortress2::TFClass_Unknown)
	{
		m_upgrademan.OnBotSpawn();
	}

	if (GetMySentryGun() == nullptr)
	{
		SetMySentryGunWaypoint(nullptr);
	}

	if (GetMyDispenser() == nullptr)
	{
		SetMyDispenserWaypoint(nullptr);
	}

	if (GetMyTeleporterEntrance() == nullptr)
	{
		SetMyTeleporterEntranceWaypoint(nullptr);
	}

	if (GetMyTeleporterExit() == nullptr)
	{
		SetMyTeleporterExitWaypoint(nullptr);
	}

	CBaseBot::Spawn();
}

void CTF2Bot::FirstSpawn()
{
	CBaseBot::FirstSpawn();
}

int CTF2Bot::GetMaxHealth() const
{
	return tf2lib::GetPlayerMaxHealth(GetIndex());
}

TeamFortress2::TFClassType CTF2Bot::GetMyClassType() const
{
	return tf2lib::GetPlayerClassType(GetIndex());
}

TeamFortress2::TFTeam CTF2Bot::GetMyTFTeam() const
{
	return tf2lib::GetEntityTFTeam(GetIndex());
}

void CTF2Bot::JoinClass(TeamFortress2::TFClassType tfclass) const
{
	auto szclass = tf2lib::GetClassNameFromType(tfclass);
	char command[128];
	ke::SafeSprintf(command, sizeof(command), "joinclass %s", szclass);
	FakeClientCommand(command);
}

void CTF2Bot::JoinTeam() const
{
	FakeClientCommand("jointeam auto");
}

edict_t* CTF2Bot::GetItem() const
{
	edict_t* item = nullptr;
	int entity = -1;

	if (entprops->GetEntPropEnt(GetIndex(), Prop_Send, "m_hItem", entity))
	{
		UtilHelpers::IndexToAThings(entity, nullptr, &item);
	}

	return item;
}

bool CTF2Bot::IsCarryingAFlag() const
{
	auto item = GetItem();

	if (item == nullptr)
		return false;

	if (strncasecmp(gamehelpers->GetEntityClassname(item), "item_teamflag", 13) == 0)
	{
		return true;
	}

	return false;
}

edict_t* CTF2Bot::GetFlagToFetch() const
{
	std::vector<int> collectedflags;
	collectedflags.reserve(16);

	if (IsCarryingAFlag())
		return GetItem();

	int flag = INVALID_EHANDLE_INDEX;

	while ((flag = UtilHelpers::FindEntityByClassname(flag, "item_teamflag")) != INVALID_EHANDLE_INDEX)
	{
		CBaseEntity* pFlag = nullptr;
		
		if (!UtilHelpers::IndexToAThings(flag, &pFlag, nullptr))
			continue;

		tfentities::HCaptureFlag eFlag(pFlag);

		if (eFlag.IsDisabled())
			continue;

		if (eFlag.IsStolen())
			continue; // ignore stolen flags

		switch (eFlag.GetType())
		{
		case TeamFortress2::TFFlagType::TF_FLAGTYPE_CTF:
			if (eFlag.GetTFTeam() == tf2lib::GetEnemyTFTeam(GetMyTFTeam()))
			{
				collectedflags.push_back(flag);
			}
			break;
		case TeamFortress2::TFFlagType::TF_FLAGTYPE_ATTACK_DEFEND:
		case TeamFortress2::TFFlagType::TF_FLAGTYPE_TERRITORY_CONTROL:
		case TeamFortress2::TFFlagType::TF_FLAGTYPE_INVADE:
			if (eFlag.GetTFTeam() != tf2lib::GetEnemyTFTeam(GetMyTFTeam()))
			{
				collectedflags.push_back(flag);
			}
			break;
		default:
			break;
		}
	}

	if (collectedflags.size() == 0)
		return nullptr;

	if (collectedflags.size() == 1)
	{
		flag = collectedflags.front();
		return gamehelpers->EdictOfIndex(flag);
	}

	flag = collectedflags[randomgen->GetRandomInt<size_t>(0U, collectedflags.size() - 1U)];
	return gamehelpers->EdictOfIndex(flag);
}

edict_t* CTF2Bot::GetFlagToDefend(bool stolenOnly) const
{
	std::vector<edict_t*> collectedflags;
	collectedflags.reserve(16);
	auto myteam = GetMyTFTeam();

	UtilHelpers::ForEachEntityOfClassname("item_teamflag", [&collectedflags, &myteam, &stolenOnly](int index, edict_t* edict, CBaseEntity* entity) {

		if (edict == nullptr)
		{
			return true; // keep loop
		}

		tfentities::HCaptureFlag flag(edict);

		if (flag.IsDisabled())
		{
			return true; // keep loop
		}

		if (stolenOnly && !flag.IsStolen())
		{
			return true; // keep loop
		}

		if (flag.GetTFTeam() == myteam)
		{
			collectedflags.push_back(edict);
		}

		return true;
	});

	if (collectedflags.empty())
	{
		return nullptr;
	}

	if (collectedflags.size() == 1)
	{
		return collectedflags[0];
	}

	return librandom::utils::GetRandomElementFromVector<edict_t*>(collectedflags);
}

/**
 * @brief Gets the capture zone position to deliver the flag
 * @return Capture zone position Vector
*/
edict_t* CTF2Bot::GetFlagCaptureZoreToDeliver() const
{
	int capturezone = INVALID_EHANDLE_INDEX;

	while ((capturezone = UtilHelpers::FindEntityByClassname(capturezone, "func_capturezone")) != INVALID_EHANDLE_INDEX)
	{
		CBaseEntity* pZone = nullptr;
		edict_t* edict = nullptr;

		if (!UtilHelpers::IndexToAThings(capturezone, &pZone, &edict))
			continue;

		tfentities::HCaptureZone entity(pZone);

		if (entity.IsDisabled())
			continue;

		if (entity.GetTFTeam() != GetMyTFTeam())
			continue;

		return edict; // return the first found
	}

	return nullptr;
}

bool CTF2Bot::IsAmmoLow() const
{
	// For engineer, check metal
	if (GetMyClassType() == TeamFortress2::TFClass_Engineer)
	{
		if (GetAmmoOfIndex(TeamFortress2::TF_AMMO_METAL) <= 0)
		{
			return true;
		}
	}

	bool haslowammoweapon = false;

	GetInventoryInterface()->ForEveryWeapon([this, &haslowammoweapon](const CBotWeapon* weapon) {
		if (haslowammoweapon)
			return;

		if (!weapon->GetWeaponInfo()->IsCombatWeapon())
		{
			return; // don't bother with ammo for non combat weapons
		}

		if (weapon->GetWeaponInfo()->HasInfiniteAmmo())
		{
			return;
		}

		if (weapon->GetWeaponInfo()->HasLowPrimaryAmmoThreshold())
		{
			if (GetAmmoOfIndex(weapon->GetBaseCombatWeapon().GetPrimaryAmmoType()) < weapon->GetWeaponInfo()->GetLowPrimaryAmmoThreshold())
			{
				haslowammoweapon = true;
				return;
			}
		}

		if (weapon->GetWeaponInfo()->HasLowSecondaryAmmoThreshold())
		{
			if (GetAmmoOfIndex(weapon->GetBaseCombatWeapon().GetSecondaryAmmoType()) < weapon->GetWeaponInfo()->GetLowSecondaryAmmoThreshold())
			{
				haslowammoweapon = true;
				return;
			}
		}
	});

	return haslowammoweapon;
}

CBaseEntity* CTF2Bot::GetMySentryGun() const
{
	return m_mySentryGun.Get();
}

CBaseEntity* CTF2Bot::GetMyDispenser() const
{
	return m_myDispenser.Get();
}

CBaseEntity* CTF2Bot::GetMyTeleporterEntrance() const
{
	return m_myTeleporterEntrance.Get();
}

CBaseEntity* CTF2Bot::GetMyTeleporterExit() const
{
	return m_myTeleporterExit.Get();
}

void CTF2Bot::SetMySentryGun(CBaseEntity* entity)
{
	m_mySentryGun = entity;
}

void CTF2Bot::SetMyDispenser(CBaseEntity* entity)
{
	m_myDispenser = entity;
}

void CTF2Bot::SetMyTeleporterEntrance(CBaseEntity* entity)
{
	m_myTeleporterEntrance = entity;
}

void CTF2Bot::SetMyTeleporterExit(CBaseEntity* entity)
{
	m_myTeleporterExit = entity;
}

void CTF2Bot::FindMyBuildings()
{
	m_mySentryGun.Term();
	m_myDispenser.Term();
	m_myTeleporterEntrance.Term();
	m_myTeleporterExit.Term();

	if (GetMyClassType() == TeamFortress2::TFClass_Engineer)
	{
		for (int i = gpGlobals->maxClients + 1; i < gpGlobals->maxEntities; i++)
		{
			auto edict = gamehelpers->EdictOfIndex(i);

			if (!UtilHelpers::IsValidEdict(edict))
				continue;

			CBaseEntity* pEntity = edict->GetIServerEntity()->GetBaseEntity();

			auto classname = entityprops::GetEntityClassname(pEntity);

			if (classname == nullptr || classname[0] == 0)
				continue;

			if (strncasecmp(classname, "obj_", 4) != 0)
				continue;

			tfentities::HBaseObject object(pEntity);

			// Placing means it still a blueprint
			if (object.IsPlacing())
				continue;

			if (strncasecmp(classname, "obj_sentrygun", 13) == 0)
			{
				if (object.GetBuilderIndex() == GetIndex())
				{
					SetMySentryGun(pEntity);
				}
			}
			else if (strncasecmp(classname, "obj_dispenser", 13) == 0)
			{
				if (object.GetBuilderIndex() == GetIndex())
				{
					SetMyDispenser(pEntity);
				}
			}
			else if (strncasecmp(classname, "obj_teleporter", 14) == 0)
			{
				if (object.GetBuilderIndex() == GetIndex())
				{
					if (object.GetMode() == TeamFortress2::TFObjectMode_Entrance)
					{
						SetMyTeleporterEntrance(pEntity);
					}
					else
					{
						SetMyTeleporterExit(pEntity);
					}
				}
			}
		}
	}
}

bool CTF2Bot::IsDisguised() const
{
	return tf2lib::IsPlayerInCondition(GetEntity(), TeamFortress2::TFCond_Disguised);
}

bool CTF2Bot::IsCloaked() const
{
	return tf2lib::IsPlayerInCondition(GetEntity(), TeamFortress2::TFCond_Cloaked);
}

bool CTF2Bot::IsInvisible() const
{
	return tf2lib::IsPlayerInvisible(GetEntity());
}

int CTF2Bot::GetCurrency() const
{
	int currency = 0;
	entprops->GetEntProp(GetIndex(), Prop_Send, "m_nCurrency", currency);
	return currency;
}

bool CTF2Bot::IsInUpgradeZone() const
{
	bool val = false;
	entprops->GetEntPropBool(GetIndex(), Prop_Send, "m_bInUpgradeZone", val);
	return val;
}

bool CTF2Bot::IsUsingSniperScope() const
{
	return tf2lib::IsPlayerInCondition(GetIndex(), TeamFortress2::TFCond_Zoomed);
}

void CTF2Bot::ToggleTournamentReadyStatus(bool isready) const
{
	char command[64];
	ke::SafeSprintf(command, sizeof(command), "tournament_player_readystate %i", isready ? 1 : 0);

	// Use 'FakeClientCommand'.
	// Alternative method is manually setting the array on gamerules
	serverpluginhelpers->ClientCommand(GetEdict(), command);
}

bool CTF2Bot::TournamentIsReady() const
{
	int index = GetIndex();
	int isready = 0;
	entprops->GameRules_GetProp("m_bPlayerReady", isready, 4, index);
	return isready != 0;
}

bool CTF2Bot::IsInsideSpawnRoom() const
{
	int result = 0;
	// this should be non-zero if a bot is touching a func_respawnroom entity
	entprops->GetEntProp(GetIndex(), Prop_Send, "m_iSpawnRoomTouchCount", result);
	return result > 0;
}



CTF2BotPathCost::CTF2BotPathCost(CTF2Bot* bot, RouteType routetype)
{
	m_me = bot;
	m_routetype = routetype;
	m_stepheight = bot->GetMovementInterface()->GetStepHeight();
	m_maxjumpheight = bot->GetMovementInterface()->GetMaxJumpHeight();
	m_maxdropheight = bot->GetMovementInterface()->GetMaxDropHeight();
	m_maxdjheight = bot->GetMovementInterface()->GetMaxDoubleJumpHeight();
	m_maxgapjumpdistance = bot->GetMovementInterface()->GetMaxGapJumpDistance();
	m_candoublejump = bot->GetMovementInterface()->IsAbleToDoubleJump();
}

float CTF2BotPathCost::operator()(CNavArea* toArea, CNavArea* fromArea, const CNavLadder* ladder, const NavOffMeshConnection* link, const CFuncElevator* elevator, float length) const
{
	if (fromArea == nullptr)
	{
		// first area in path, no cost
		return 0.0f;
	}

	CTFNavArea* area = static_cast<CTFNavArea*>(toArea);

	if (!m_me->GetMovementInterface()->IsAreaTraversable(area))
	{
		return -1.0f;
	}

	float dist = 0.0f;

	if (link != nullptr)
	{
		dist = link->GetConnectionLength();
	}
	else if (length > 0.0f)
	{
		dist = length;
	}
	else
	{
		dist = (toArea->GetCenter() + fromArea->GetCenter()).Length();
	}

	// only check gap and height on common connections
	if (link == nullptr)
	{
		float deltaZ = fromArea->ComputeAdjacentConnectionHeightChange(area);

		if (deltaZ >= m_stepheight)
		{
			if (deltaZ >= m_maxjumpheight && !m_candoublejump) // can't double jump
			{
				// too high to reach
				return -1.0f;
			}

			if (deltaZ >= m_maxdjheight) // can double jump
			{
				// too high to reach
				return -1.0f;
			}

			// jump type is resolved by the navigator

			// add jump penalty
			constexpr auto jumpPenalty = 2.0f;
			dist *= jumpPenalty;
		}
		else if (deltaZ < -m_maxdropheight)
		{
			// too far to drop
			return -1.0f;
		}

		float gap = fromArea->ComputeAdjacentConnectionGapDistance(area);

		if (gap >= m_maxgapjumpdistance)
		{
			return -1.0f; // can't jump over this gap
		}
	}
	else
	{
		// Don't use double jump links if we can't perform a double jump
		if (link->GetType() == OffMeshConnectionType::OFFMESH_DOUBLE_JUMP && !m_candoublejump)
		{
			return -1.0f;
		}

		// TO-DO: Same check for when rocket jumps are implemented.
	}

	if (area->HasTFPathAttributes(CTFNavArea::TFNAV_PATH_NO_CARRIERS))
	{
		if (m_me->IsCarryingAFlag())
		{
			return -1.0f;
		}
	}

	if (area->HasTFPathAttributes(CTFNavArea::TFNAV_PATH_CARRIERS_AVOID))
	{
		if (m_me->IsCarryingAFlag())
		{
			constexpr auto pathflag_carrier_avoid = 7.0f;
			dist *= pathflag_carrier_avoid;
		}
	}

	float cost = dist + fromArea->GetCostSoFar();

	return cost;
}

CTFWaypoint* CTF2Bot::GetMySentryGunWaypoint() const
{
	return m_sgWaypoint;
}

CTFWaypoint* CTF2Bot::GetMyDispenserWaypoint() const
{
	return m_dispWaypoint;
}

CTFWaypoint* CTF2Bot::GetMyTeleporterEntranceWaypoint() const
{
	return m_tpentWaypoint;
}

CTFWaypoint* CTF2Bot::GetMyTeleporterExitWaypoint() const
{
	return m_tpextWaypoint;
}

void CTF2Bot::SetMySentryGunWaypoint(CTFWaypoint* wpt)
{
	m_sgWaypoint = wpt;
}

void CTF2Bot::SetMyDispenserWaypoint(CTFWaypoint* wpt)
{
	m_dispWaypoint = wpt;
}

void CTF2Bot::SetMyTeleporterEntranceWaypoint(CTFWaypoint* wpt)
{
	m_tpentWaypoint = wpt;
}

void CTF2Bot::SetMyTeleporterExitWaypoint(CTFWaypoint* wpt)
{
	m_tpextWaypoint = wpt;
}
