#include <extension.h>
#include <ifaces_extern.h>
#include <extplayer.h>
#include <bot/interfaces/base_interface.h>
#include <bot/interfaces/knownentity.h>
#include <bot/interfaces/playerinput.h>
#include "basebot.h"

extern CGlobalVars* gpGlobals;

CBaseBot::CBaseBot(edict_t* edict) : CBaseExtPlayer(edict),
	m_cmd(),
	m_viewangles(0.0f, 0.0f, 0.0f)
{
	m_nextupdatetime = 64;
	m_controller = botmanager->GetBotController(edict);
	m_basecontrol = nullptr;
	m_basemover = nullptr;
	m_basesensor = nullptr;
	m_weaponselect = 0;
}

CBaseBot::~CBaseBot()
{
	if (m_basecontrol)
	{
		delete m_basecontrol;
	}

	if (m_basemover)
	{
		delete m_basemover;
	}

	if (m_basesensor)
	{
		delete m_basesensor;
	}
	
	m_interfaces.clear();
}

void CBaseBot::PlayerThink()
{
	CBaseExtPlayer::PlayerThink(); // Call base class function first

	Frame(); // Call bot frame

	int buttons = 0;
	auto control = GetControlInterface();

	if (--m_nextupdatetime <= 0)
	{
		m_nextupdatetime = TIME_TO_TICKS(BOT_UPDATE_INTERVAL);

		Update(); // Run period update

		// Process all buttons during updates
		control->ProcessButtons(buttons);
	}
	else // Not running a full update
	{
		// Keep last cmd buttons pressed + any new button pressed
		buttons = control->GetOldButtonsToSend();
	}

	// This needs to be the last call on a bot think cycle
	BuildUserCommand(buttons);
}

CBaseBot* CBaseBot::MyBotPointer()
{
	return this;
}

void CBaseBot::Reset()
{
	m_nextupdatetime = 1;

	for (auto iface : m_interfaces)
	{
		iface->Reset();
	}
}

void CBaseBot::Update()
{
	for (auto iface : m_interfaces)
	{
		iface->Update();
	}
}

void CBaseBot::Frame()
{
	for (auto iface : m_interfaces)
	{
		iface->Frame();
	}
}

void CBaseBot::RegisterInterface(IBotInterface* iface)
{
	m_interfaces.push_back(iface);
}

void CBaseBot::BuildUserCommand(const int buttons)
{
	int commandnumber = m_cmd.command_number;
	auto mover = GetMovementInterface();
	auto control = GetControlInterface();

	m_cmd.Reset();

	commandnumber++;
	m_cmd.command_number = commandnumber;
	m_cmd.tick_count = gpGlobals->tickcount;
	m_cmd.impulse = 0;
	m_cmd.viewangles = m_viewangles; // set view angles
	m_cmd.weaponselect = m_weaponselect; // send weapon select
	// TO-DO: weaponsubtype
	m_cmd.buttons = buttons; // send buttons

	float forwardspeed = 0.0f;
	float sidespeed = 0.0f;

	if (buttons & INPUT_FORWARD)
	{
		forwardspeed = mover->GetMovementSpeed();
	}
	else if (buttons & INPUT_BACK)
	{
		forwardspeed = -mover->GetMovementSpeed();
	}

	if (buttons & INPUT_MOVERIGHT)
	{
		sidespeed = mover->GetMovementSpeed();
	}
	else if (buttons & INPUT_MOVELEFT)
	{
		sidespeed = -mover->GetMovementSpeed();
	}

	// TO-DO: Up speed

	if (control->ShouldApplyScale())
	{
		forwardspeed = forwardspeed * control->GetForwardScale();
		sidespeed = sidespeed * control->GetSideScale();
	}

	m_cmd.forwardmove = forwardspeed;
	m_cmd.sidemove = sidespeed;
	m_cmd.upmove = 0.0f;

	m_controller->RunPlayerMove(&m_cmd);

	m_weaponselect = 0;
}

IPlayerController* CBaseBot::GetControlInterface()
{
	if (m_basecontrol == nullptr)
	{
		m_basecontrol = new IPlayerController(this);
	}

	return m_basecontrol;
}

IMovement* CBaseBot::GetMovementInterface()
{
	if (m_basemover == nullptr)
	{
		m_basemover = new IMovement(this);
	}

	return m_basemover;
}

ISensor* CBaseBot::GetSensorInterface()
{
	if (m_basesensor == nullptr)
	{
		m_basesensor = new ISensor(this);
	}

	return m_basesensor;
}
