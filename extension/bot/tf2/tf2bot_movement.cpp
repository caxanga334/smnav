#include <extension.h>
#include <sdkports/debugoverlay_shared.h>
#include <bot/tf2/tf2bot.h>
#include <mods/tf2/tf2lib.h>
#include <entities/tf2/tf_entities.h>
#include "tf2bot_movement.h"

static ConVar sm_navbot_tf_double_jump_z_boost("sm_navbot_tf_double_jump_z_boost", "300.0", FCVAR_CHEAT, "Amount of Z velocity to add when double jumping.");

CTF2BotMovement::CTF2BotMovement(CBaseBot* bot) : IMovement(bot)
{
	m_initialJumpWasOnGround = false;
}

CTF2BotMovement::~CTF2BotMovement()
{
}

void CTF2BotMovement::Reset()
{
	m_doublejumptimer.Invalidate();
	m_djboosttimer.Invalidate();
	m_initialJumpWasOnGround = false;
	IMovement::Reset();
}

void CTF2BotMovement::Frame()
{

	if (m_doublejumptimer.HasStarted() && m_doublejumptimer.IsElapsed())
	{
		if (m_initialJumpWasOnGround)
		{
			m_djboosttimer.Start(0.15f);
			m_initialJumpWasOnGround = false;
		}

		Jump();
		GetBot()->DebugPrintToConsole(BOTDEBUG_MOVEMENT, 200, 255, 200, "%s Double Jump! \n", GetBot()->GetDebugIdentifier());
		m_doublejumptimer.Invalidate();
	}


	if (m_djboosttimer.HasStarted() && m_djboosttimer.IsElapsed())
	{
		Vector vel = GetBot()->GetAbsVelocity();
		vel.z = sm_navbot_tf_double_jump_z_boost.GetFloat();
		GetBot()->SetAbsVelocity(vel);
		GetBot()->DebugPrintToConsole(BOTDEBUG_MOVEMENT, 200, 255, 200, "%s Double Jump (Z BOOST)! \n", GetBot()->GetDebugIdentifier());
		m_djboosttimer.Invalidate();
	}

	IMovement::Frame();
}

float CTF2BotMovement::GetHullWidth()
{
	float scale = GetBot()->GetModelScale();
	return PLAYER_HULL_WIDTH * scale;
}

float CTF2BotMovement::GetStandingHullHeigh()
{
	float scale = GetBot()->GetModelScale();
	return PLAYER_HULL_STAND * scale;
}

float CTF2BotMovement::GetCrouchedHullHeigh()
{
	float scale = GetBot()->GetModelScale();
	return PLAYER_HULL_CROUCH * scale;
}

float CTF2BotMovement::GetMaxGapJumpDistance() const
{
	auto cls = tf2lib::GetPlayerClassType(GetBot()->GetIndex());

	switch (cls)
	{
	case TeamFortress2::TFClass_Scout:
		return 600.0f; // double jump gap distance
	case TeamFortress2::TFClass_Soldier:
		return 214.0f;
	case TeamFortress2::TFClass_DemoMan:
		return 240.0f;
	case TeamFortress2::TFClass_Medic:
		return 270.0f;
	case TeamFortress2::TFClass_Heavy:
		return 207.0f;
	case TeamFortress2::TFClass_Spy:
		return 270.0f;
	default:
		return 257.0f; // classes that moves at 'default speed' (engineer, sniper, pyro)
	}
}

void CTF2BotMovement::DoubleJump()
{
	CrouchJump(); // crouch jump first
	m_doublejumptimer.Start(0.55f);
	m_initialJumpWasOnGround = IsOnGround();
}

bool CTF2BotMovement::IsAbleToDoubleJump()
{
	auto cls = tf2lib::GetPlayerClassType(GetBot()->GetIndex());

	if (cls == TeamFortress2::TFClass_Scout)
	{
		return true;
	}

	return false;
}

bool CTF2BotMovement::IsAbleToBlastJump()
{
	auto cls = tf2lib::GetPlayerClassType(GetBot()->GetIndex());

	// TO-DO: Demoman and maybe engineer
	if (cls == TeamFortress2::TFClass_Soldier)
	{
		if (GetBot()->GetHealth() < min_health_for_rocket_jumps())
		{
			return false;
		}

		return true;
	}

	return false;
}

void CTF2BotMovement::JumpAcrossGap(const Vector& landing, const Vector& forward)
{
	Jump();

	// look towards the jump target
	GetBot()->GetControlInterface()->AimAt(landing, IPlayerController::LOOK_MOVEMENT, 1.0f);

	m_isJumpingAcrossGap = true;
	m_landingGoal = landing;
	m_isAirborne = false;

	if (GetTF2Bot()->GetMyClassType() == TeamFortress2::TFClass_Scout)
	{
		float jumplength = (GetBot()->GetAbsOrigin() - landing).Length();

		if (jumplength >= scout_gap_jump_do_double_distance())
		{
			m_doublejumptimer.Start(0.5f);
		}
	}

	if (GetBot()->IsDebugging(BOTDEBUG_MOVEMENT))
	{
		Vector top = GetBot()->GetAbsOrigin();
		top.z += GetMaxJumpHeight();

		NDebugOverlay::VertArrow(GetBot()->GetAbsOrigin(), top, 5.0f, 0, 0, 255, 255, false, 5.0f);
		NDebugOverlay::HorzArrow(top, landing, 5.0f, 0, 255, 0, 255, false, 5.0f);
	}
}

bool CTF2BotMovement::IsEntityTraversable(int index, edict_t* edict, CBaseEntity* entity, const bool now)
{
	auto theirteam = tf2lib::GetEntityTFTeam(index);
	auto myteam = GetTF2Bot()->GetMyTFTeam();

	if (myteam == theirteam)
	{
		/* TO-DO: check solid teammates cvar */
		if (UtilHelpers::IsPlayerIndex(index))
		{
			return true;
		}

		if (UtilHelpers::FClassnameIs(entity, "obj_*"))
		{
			if (GetTF2Bot()->GetMyClassType() == TeamFortress2::TFClassType::TFClass_Engineer)
			{
				CBaseEntity* builder = tf2lib::GetBuildingBuilder(entity);

				if (builder == GetTF2Bot()->GetEntity())
				{
					return false; // my own buildings are solid to me
				}

				return true; // not my own buildings, not solid
			}

			return true; // not an engineer, friendly buildings are not solid (the telepoter is solid but we can generally walk over it)
		}
	}

	return IMovement::IsEntityTraversable(index, edict, entity, now);
}

CTF2Bot* CTF2BotMovement::GetTF2Bot() const
{
	return static_cast<CTF2Bot*>(GetBot());
}

