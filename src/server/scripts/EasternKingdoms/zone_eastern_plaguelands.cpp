/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* ScriptData
SDName: Eastern_Plaguelands
SD%Complete: 100
SDComment: Quest support: 5211, 5742. Special vendor Augustus the Touched
SDCategory: Eastern Plaguelands
EndScriptData */

/* ContentData
npc_ghoul_flayer
npc_augustus_the_touched
npc_darrowshire_spirit
npc_tirion_fordring
EndContentData */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "WorldSession.h"

class npc_ghoul_flayer : public CreatureScript
{
public:
    npc_ghoul_flayer() : CreatureScript("npc_ghoul_flayer") { }

    struct npc_ghoul_flayerAI : public ScriptedAI
    {
        npc_ghoul_flayerAI(Creature* creature) : ScriptedAI(creature) { }

        void Reset() OVERRIDE {}

        void EnterCombat(Unit* /*who*/) OVERRIDE {}

        void JustDied(Unit* killer) OVERRIDE
        {
            if (killer->GetTypeId() == TYPEID_PLAYER)
                me->SummonCreature(11064, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN, 60000);
        }
    };

    CreatureAI* GetAI(Creature* creature) const OVERRIDE
    {
        return new npc_ghoul_flayerAI(creature);
    }
};

/*######
## npc_augustus_the_touched
######*/

class npc_augustus_the_touched : public CreatureScript
{
public:
    npc_augustus_the_touched() : CreatureScript("npc_augustus_the_touched") { }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) OVERRIDE
    {
        player->PlayerTalkClass->ClearMenus();
        if (action == GOSSIP_ACTION_TRADE)
            player->GetSession()->SendListInventory(creature->GetGUID());
        return true;
    }

    bool OnGossipHello(Player* player, Creature* creature) OVERRIDE
    {
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        if (creature->IsVendor() && player->GetQuestRewardStatus(6164))
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_VENDOR, GOSSIP_TEXT_BROWSE_GOODS, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

        player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }
};

/*######
## npc_darrowshire_spirit
######*/

enum DarrowshireSpirit
{
    SPELL_SPIRIT_SPAWNIN    = 17321
};

class npc_darrowshire_spirit : public CreatureScript
{
public:
    npc_darrowshire_spirit() : CreatureScript("npc_darrowshire_spirit") { }

    bool OnGossipHello(Player* player, Creature* creature) OVERRIDE
    {
        player->SEND_GOSSIP_MENU(3873, creature->GetGUID());
        player->TalkedToCreature(creature->GetEntry(), creature->GetGUID());
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        return true;
    }

    CreatureAI* GetAI(Creature* creature) const OVERRIDE
    {
        return new npc_darrowshire_spiritAI(creature);
    }

    struct npc_darrowshire_spiritAI : public ScriptedAI
    {
        npc_darrowshire_spiritAI(Creature* creature) : ScriptedAI(creature) {}

        void Reset() OVERRIDE
        {
            DoCast(me, SPELL_SPIRIT_SPAWNIN);
            me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        }

        void EnterCombat(Unit* /*who*/) OVERRIDE {}
    };
};

/*######
## npc_tirion_fordring
######*/

#define GOSSIP_HELLO    "I am ready to hear your tale, Tirion."
#define GOSSIP_SELECT1  "Thank you, Tirion.  What of your identity?"
#define GOSSIP_SELECT2  "That is terrible."
#define GOSSIP_SELECT3  "I will, Tirion."

class npc_tirion_fordring : public CreatureScript
{
public:
    npc_tirion_fordring() : CreatureScript("npc_tirion_fordring") { }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) OVERRIDE
    {
        player->PlayerTalkClass->ClearMenus();
        switch (action)
        {
            case GOSSIP_ACTION_INFO_DEF+1:
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_SELECT1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
                player->SEND_GOSSIP_MENU(4493, creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF+2:
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_SELECT2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
                player->SEND_GOSSIP_MENU(4494, creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF+3:
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_SELECT3, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 4);
                player->SEND_GOSSIP_MENU(4495, creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF+4:
                player->CLOSE_GOSSIP_MENU();
                player->AreaExploredOrEventHappens(5742);
                break;
        }
        return true;
    }

    bool OnGossipHello(Player* player, Creature* creature) OVERRIDE
    {
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        if (player->GetQuestStatus(5742) == QUEST_STATUS_INCOMPLETE && player->getStandState() == UNIT_STAND_STATE_SIT)
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_HELLO, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+1);

        player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());

        return true;
    }
};

enum ErisHavenfireMisc
{
    QUEST_THE_BALANCE_OF_LIGHT_AND_SHADOW   = 7622,
    NPC_PEASANT                             = 14485,
    NPC_FOOTSOLDIER                         = 14486,
    ACTION_START_SPAWN                      = 0,
    EVENT_SPAWN_PEASANT                     = 1,
    FAIL_QUEST_EVENT                        = 2,
    COMPLETE_QUEST_EVENT                    = 3,
};

Position const PeasantsPos[12] =
{
    { 3364.47f, -3048.50f, 165.17f, 1.86f },
    { 3363.242f, -3052.06f, 165.264f, 2.095f },
    { 3362.33f, -3049.37f, 165.23f, 1.54f },
    { 3360.13f, -3052.63f, 165.31f, 1.88f },
    { 3361.05f, -3055.49f, 165.31f, 2.041f },
    { 3363.92f, -3046.96f, 165.09f, 2.13f },
    { 3366.83f, -3052.23f, 165.41f, 2.044f },
    { 3367.79f, -3047.80f, 165.16f, 2.08f },
    { 3358.76f, -3050.37f, 165.2f, 2.05f },
    { 3366.63f, -3045.29f, 164.99f, 2.19f },
    { 3357.45f, -3052.82f, 165.50f, 2.006f },
    { 3363.00f, -3044.21f, 164.80f, 2.182f },
};

class npc_eris_havenfire : public CreatureScript
{
public:
    npc_eris_havenfire() : CreatureScript("npc_eris_havenfire") { }

    bool OnQuestAccept(Player* player, Creature* creature, Quest const* quest)
    {
        if (quest->GetQuestId() == QUEST_THE_BALANCE_OF_LIGHT_AND_SHADOW)
        {
            creature->AI()->DoAction(ACTION_START_SPAWN);
        }
        return true;
    }

    struct npc_eris_havenfireAI : public ScriptedAI
    {
        npc_eris_havenfireAI(Creature* creature) : ScriptedAI(creature) { }

        EventMap events;
        uint8 failCounter;
        uint8 winCounter;
        uint8 SavePeasants;
        uint8 DeadPeasants;

        void Reset()
        {
            events.Reset();
            failCounter     = 0;
            winCounter      = 0;
            SavePeasants    = 50;
            DeadPeasants    = 15;

        }

        void SpawnPeasants()
        {
            for (int i = 0; i < 12; ++i)
                me->SummonCreature(NPC_PEASANT, PeasantsPos[i], TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 600000);
        }

        void CompleteQuest()
        {
            if (Map *map = me->GetMap())
            {
                Map::PlayerList const &PlayerList = map->GetPlayers();
                for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                {
                    if (Player *player = i->GetSource())
                    {
                        if (me->IsInRange(player, 0.0f, 200.0f))
                            player->CompleteQuest(QUEST_THE_BALANCE_OF_LIGHT_AND_SHADOW);
                    }
                }
            }
        }

        void FailQuest()
        {
            std::list<Player*> players = me->GetNearestPlayersList(200.0f);
            for (std::list<Player*>::const_iterator pitr = players.begin(); pitr != players.end(); ++pitr)
            {
                Player* player = *pitr;
                if (player->GetQuestStatus(QUEST_THE_BALANCE_OF_LIGHT_AND_SHADOW) == QUEST_STATUS_INCOMPLETE)
                {
                    player->FailQuest(QUEST_THE_BALANCE_OF_LIGHT_AND_SHADOW);
                }
            }
        }

        void DespawnSummons()
        {
            std::list<Creature*> SummonList;
            me->GetCreatureListWithEntryInGrid(SummonList, NPC_PEASANT, 1000.0f);
            me->GetCreatureListWithEntryInGrid(SummonList, NPC_FOOTSOLDIER, 1000.0f);
            if (!SummonList.empty())
                for (std::list<Creature*>::iterator itr = SummonList.begin(); itr != SummonList.end(); itr++)
                    (*itr)->DespawnOrUnsummon();
        }

        void SetData(uint32 type, uint32 data)
        {
            if (type == 1 && data == 1)
            {
                ++failCounter;
            }

            if (type == 2 && data == 2)
            {
                ++winCounter;
            }
        }

        void DoAction(int32 action)
        {
            if (action == ACTION_START_SPAWN)
            {
                events.ScheduleEvent(EVENT_SPAWN_PEASANT, 2 * IN_MILLISECONDS);
            }
        }

        void UpdateAI(uint32 diff)
        {

            if (winCounter == SavePeasants)
            {
                EnterEvadeMode();
                DespawnSummons();
                CompleteQuest();
            }

            if (failCounter == DeadPeasants)
            {
                EnterEvadeMode();
                DespawnSummons();
                FailQuest();
            }

            events.Update(diff);

            switch (events.ExecuteEvent())
            {
                case EVENT_SPAWN_PEASANT:
                    SpawnPeasants();
                    events.ScheduleEvent(EVENT_SPAWN_PEASANT, 60 * IN_MILLISECONDS);
                    break;
                default:
                    break;
            }

            if (UpdateVictim())
            {
                DoMeleeAttackIfReady();
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_eris_havenfireAI(creature);
    }

};

void AddSC_eastern_plaguelands()
{
    new npc_ghoul_flayer();
    new npc_augustus_the_touched();
    new npc_darrowshire_spirit();
    new npc_tirion_fordring();
    new npc_eris_havenfire();
}
