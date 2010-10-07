/* Copyright (C) 2006 - 2008 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* ScriptData
SDName: Boss_Reliquary_of_Souls
SD%Complete: 90
SDComment:
SDCategory: Black Temple
EndScriptData */

#include "precompiled.h"
#include "def_black_temple.h"
#include "Spell.h"

//Sound'n'speech
//Suffering
#define SUFF_SAY_FREED              -1564047
#define SUFF_SAY_AGGRO              -1564048
#define SUFF_SAY_SLAY1              -1564049
#define SUFF_SAY_SLAY2              -1564050
#define SUFF_SAY_SLAY3              -1564051
#define SUFF_SAY_RECAP              -1564052
#define SUFF_SAY_AFTER              -1564053
#define SUFF_EMOTE_ENRAGE           -1564054

//Desire
#define DESI_SAY_FREED              -1564055
#define DESI_SAY_SLAY1              -1564056
#define DESI_SAY_SLAY2              -1564057
#define DESI_SAY_SLAY3              -1564058
#define DESI_SAY_SPEC               -1564059
#define DESI_SAY_RECAP              -1564060
#define DESI_SAY_AFTER              -1564061

//Anger
#define ANGER_SAY_FREED             -1564062
#define ANGER_SAY_FREED2            -1564063
#define ANGER_SAY_SLAY1             -1564064
#define ANGER_SAY_SLAY2             -1564065
#define ANGER_SAY_SPEC              -1564066
#define ANGER_SAY_BEFORE            -1564067
#define ANGER_SAY_DEATH             -1564068

//Spells
#define AURA_OF_SUFFERING               41292
#define AURA_OF_SUFFERING_ARMOR         42017 // linked aura, need core support
#define ESSENCE_OF_SUFFERING_PASSIVE    41296 // periodic trigger 41294
#define ESSENCE_OF_SUFFERING_PASSIVE2   41623
#define SPELL_FIXATE_TARGET             41294 // dummy, select target
#define SPELL_FIXATE_TAUNT              41295 // force taunt
#define SPELL_ENRAGE                    41305
#define SPELL_SOUL_DRAIN                41303

#define AURA_OF_DESIRE                  41350
#define AURA_OF_DESIRE_DAMAGE           41352
#define SPELL_RUNE_SHIELD               41431
#define SPELL_DEADEN                    41410
#define SPELL_SOUL_SHOCK                41426

#define AURA_OF_ANGER                   41337
#define SPELL_SELF_SEETHE               41364 // force cast 41520
#define SPELL_ENEMY_SEETHE              41520
#define SPELL_SOUL_SCREAM               41545
#define SPELL_SPITE_TARGET              41376 // cast 41377 after 6 sec
#define SPELL_SPITE_DAMAGE              41377

#define ENSLAVED_SOUL_PASSIVE           41535
#define SPELL_SOUL_RELEASE              41542
#define SPELL_SUBMERGE                  37550 //dropout 'head'

#define CREATURE_ENSLAVED_SOUL          23469
#define NUMBER_ENSLAVED_SOUL            16

#define ROS_TRIGGER_ID                  23472
#define SPELL_SUMMON_ENSLAVE_SOUL       41538

struct cPosition
{
    float x,y;
};

struct TRINITY_DLL_DECL npc_enslaved_soulAI : public ScriptedAI
{
    npc_enslaved_soulAI(Creature *c) : ScriptedAI(c)
    {
        pInstance = (ScriptedInstance*)c->GetInstanceData();
    }

    ScriptedInstance *pInstance;
    uint32 checkTimer;

    void Reset()
    {
        checkTimer = 3000;
    }

    void EnterCombat(Unit* who)
    {
        DoCast(m_creature, ENSLAVED_SOUL_PASSIVE, true);
    }

    void JustRespawned()
    {
        if(pInstance)
        {
            DoZoneInCombat();

            if(Unit * target = SelectUnit(SELECT_TARGET_RANDOM, 0, 200, true))
                AttackStart(target);
        }
    }

    void JustDied(Unit *killer)
    {
        if(pInstance)
            pInstance->SetData(DATA_ENSLAVED_SOUL, 1);

        DoCast(m_creature, SPELL_SOUL_RELEASE, true);
    }

    void UpdateAI(const uint32 diff)
    {
        if (checkTimer <= diff)
        {
            if (pInstance)
            {
                if (pInstance->GetData(DATA_RELIQUARYOFSOULSEVENT) != IN_PROGRESS)
                    m_creature->Kill(m_creature, false);
            }
            else
                m_creature->Kill(m_creature, false);

            checkTimer = 3000;
        }
        else
            checkTimer -= diff;

        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

struct TRINITY_DLL_DECL boss_reliquary_of_soulsAI : public Scripted_NoMovementAI
{
    boss_reliquary_of_soulsAI(Creature *c) : Scripted_NoMovementAI(c)
    {
        pInstance = ((ScriptedInstance*)c->GetInstanceData());
        EssenceGUID = 0;
        m_creature->setActive(true);
    }

    ScriptedInstance* pInstance;

    uint64 EssenceGUID;

    uint32 Phase;
    uint32 Counter;
    uint32 Timer;

    uint32 SoulCount;
    uint32 SoulDeathCount;

    uint32 CheckTimer;
    uint32 DelayTimer;

    void Reset()
    {
        if(pInstance)
            pInstance->SetData(DATA_RELIQUARYOFSOULSEVENT, NOT_STARTED);

        if(EssenceGUID)
        {
            if(Unit* Essence = Unit::GetUnit(*m_creature, EssenceGUID))
            {
                Essence->SetVisibility(VISIBILITY_OFF);
                Essence->setDeathState(DEAD);
            }
            EssenceGUID = 0;
        }

        Phase = 0;

        CheckTimer = 2000;
        DelayTimer = 0;

        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        m_creature->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
        m_creature->RemoveAurasDueToSpell(SPELL_SUBMERGE);
    }

    void StartEvent(Unit *who)
    {
        if(!m_creature->isInCombat())
        {
            if (m_creature->IsWithinDistInMap(who, 100))
            {
                m_creature->AddThreat(who, 10000.0f);
                DoZoneInCombat();

                if(pInstance)
                    pInstance->SetData(DATA_RELIQUARYOFSOULSEVENT, IN_PROGRESS);
                Phase = 1;
                Counter = 0;
                Timer = 0;
                DelayTimer = 15000;
            }
        }
        
    }

    void EnterCombat(Unit* who)
    {
        m_creature->SetUInt64Value(UNIT_FIELD_TARGET, NULL);
    }

    void SummonSouls()
    {
        std::list<Creature*> triggerList = DoFindAllCreaturesWithEntry(ROS_TRIGGER_ID, 100);

        for (int i = 0; i < NUMBER_ENSLAVED_SOUL; i++)
        {
            uint32 random = rand()%triggerList.size();

            std::list<Creature*>::iterator itr = triggerList.begin();

            advance(itr, random);

            Creature * trigger = *itr;
            if (trigger)
                trigger->CastSpell(trigger, SPELL_SUMMON_ENSLAVE_SOUL, false);
        }
    }

    void JustDied(Unit* killer)
    {
        if(pInstance)
            pInstance->SetData(DATA_RELIQUARYOFSOULSEVENT, DONE);
    }

    bool FindPlayers()
    {
        std::list<HostilReference*>& m_threatlist = m_creature->getThreatManager().getThreatList();
        if(m_threatlist.empty())
            return false;

        for(std::list<HostilReference*>::iterator itr = m_threatlist.begin(); itr != m_threatlist.end(); ++itr)
        {
            Unit* pUnit = Unit::GetUnit((*m_creature), (*itr)->getUnitGuid());
            if(pUnit && pUnit->isAlive() && pUnit->isInCombat() && m_creature->canAttack(pUnit) && pUnit->IsWithinDistInMap(m_creature, 100.0f))
                return true;
        }
        return false;
    }

    void UpdateAI(const uint32 diff)
    {
        if(DelayTimer > diff)
        {
            DelayTimer -= diff;
            return;
        }
        else
            DelayTimer = 0;

        if(!Phase)
            return;

        if(CheckTimer < diff)
        {
            if(FindPlayers())
                DoZoneInCombat();
            else
            {
                if(EssenceGUID)
                {
                    if(Creature *Essence = Unit::GetCreature(*m_creature, EssenceGUID))
                    {
                        Essence->Kill(Essence, false);
                        Essence->RemoveCorpse();
                    }
                }

                EnterEvadeMode();
                return;
            }

            m_creature->SetUInt64Value(UNIT_FIELD_TARGET, NULL);

            CheckTimer = 2000;
        }
        else
            CheckTimer -= diff;


        if(EssenceGUID)
        {
            Creature* Essence = Unit::GetCreature(*m_creature, EssenceGUID);
            if(!Essence)
            {
                EnterEvadeMode();
                return;
            }
        }

        if(Timer < diff)
        {
            switch(Counter)
            {
            case 0:
                m_creature->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_READY2H);  // I R ANNNGRRRY!
                Timer = 3000;
                break;
            case 1:
                Timer = 2800;
                DoCast(m_creature,SPELL_SUBMERGE);
                break;
            case 2:
            {
                Timer = 5000;
                if(Creature* Summon = DoSpawnCreature(23417+Phase, 0, 0, 0, 0, TEMPSUMMON_DEAD_DESPAWN, 0))
                {
                    if(Unit* target = SelectUnit(SELECT_TARGET_TOPAGGRO, 0))
                    {
                        Summon->AI()->AttackStart(target);
                        EssenceGUID = Summon->GetGUID();
                    }
                }
                else
                {
                    EnterEvadeMode();
                    return;
                }
                break;
            }
            case 3:
            {
                Creature* Essence = Unit::GetCreature(*m_creature, EssenceGUID);
                if(!Essence)
                    return;

                Timer = 1000;
                if(Phase == 3)
                {
                    if(!Essence->isAlive())
                        DoCast(m_creature, 7, true);
                    else
                        return;
                }
                else
                {
                    if(Essence->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE))
                        Essence->GetMotionMaster()->MovePoint(0, m_creature->GetPositionX(),m_creature->GetPositionY(), m_creature->GetPositionZ());
                    else
                        return;
                }
                break;
            }
            case 4:
            {
                Creature* Essence = Unit::GetCreature(*m_creature, EssenceGUID);
                if(!Essence)
                    return;

                Timer = 1500;
                if(Essence->IsWithinDistInMap(m_creature, 3))
                    m_creature->RemoveAurasDueToSpell(SPELL_SUBMERGE);
                else
                {
                    Essence->GetMotionMaster()->MovePoint(0, m_creature->GetPositionX(),m_creature->GetPositionY(), m_creature->GetPositionZ());
                    return;
                }
                break;
            }
            case 5:
            {
                Creature* Essence = Unit::GetCreature(*m_creature, EssenceGUID);
                if(!Essence)
                    return;

                if(Phase == 1)
                    DoScriptText(SUFF_SAY_AFTER, Essence);
                else
                    DoScriptText(DESI_SAY_AFTER, Essence);

                Essence->SetVisibility(VISIBILITY_OFF);
                Essence->DestroyForNearbyPlayers();
                Essence->setDeathState(DEAD);
                m_creature->SetUInt32Value(UNIT_NPC_EMOTESTATE,0);

                EssenceGUID = 0;
                SoulCount = 0;
                SoulDeathCount = 0;
                Timer = 3000;
                break;
            }
            case 6:
                SummonSouls();
                Timer = urand(35000, 40000);//200;
                break;
            case 7:
                /*if(pInstance->GetData(DATA_ENSLAVED_SOUL) >= SoulCount)
                {*/
                    Counter = 1;
                    Phase++;
                    Timer = 5000;
                //}
                return;
            default:
                break;
            }
            Counter++;
        }
        else
            Timer -= diff;
    }
};

struct TRINITY_DLL_DECL npc_ros_triggerAI : public ScriptedAI
{
    npc_ros_triggerAI(Creature *c) : ScriptedAI(c)
    {
        pInstance = (ScriptedInstance*)c->GetInstanceData();
    }

    ScriptedInstance *pInstance;
    uint64 RosGUID;

    void Reset()
    {
        RosGUID = 0;
    }

    void EnterCombat(Unit* who){}

    void JustRespawned(){}

    void MoveInLineOfSight(Unit * who)
    {
        if (!RosGUID)
            RosGUID = pInstance->GetData64(DATA_RELIQUARYOFSOULSEVENT);

        if (!pInstance)
             pInstance = (ScriptedInstance*)m_creature->GetInstanceData();

        if (pInstance && pInstance->GetData(DATA_RELIQUARYOFSOULSEVENT) == NOT_STARTED && who->GetTypeId() == TYPEID_PLAYER)
        {
            Creature * ros = Unit::GetCreature(*m_creature, RosGUID);
            if (ros && !((Player*)who)->isGameMaster())
            {
                ((boss_reliquary_of_soulsAI*)(ros->AI()))->StartEvent(who);
            }
        }
    }

    void JustDied(Unit *killer){}

    void UpdateAI(const uint32 diff){}
};

//This is used to sort the players by distance in preparation for the Fixate cast.
struct TargetDistanceOrder : public std::binary_function<const Unit, const Unit, bool>
{
    const Unit* MainTarget;
    TargetDistanceOrder(const Unit* Target) : MainTarget(Target) {};
    // functor for operator "<"
    bool operator()(const Unit* _Left, const Unit* _Right) const
    {
        return (MainTarget->GetDistance(_Left) < MainTarget->GetDistance(_Right));
    }
};

struct TRINITY_DLL_DECL boss_essence_of_sufferingAI : public ScriptedAI
{
    boss_essence_of_sufferingAI(Creature *c) : ScriptedAI(c)
    {
        SpellEntry *tempSpell = (SpellEntry*)GetSpellStore()->LookupEntry(ESSENCE_OF_SUFFERING_PASSIVE);
        if(tempSpell)
            tempSpell->EffectTriggerSpell[1] = 0;
    }

    uint64 StatAuraGUID;
    uint64 FixateVictimGUID;

    uint32 AggroYellTimer;
    uint32 FixateTimer;
    uint32 EnrageTimer;
    uint32 SoulDrainTimer;
    uint32 AuraTimer;
    uint32 checkTimer;

    bool emoteDone;
    bool backToCage;

    void Reset()
    {
        StatAuraGUID = 0;
        FixateVictimGUID = 0;

        AggroYellTimer = 5000;
        FixateTimer = 5000;
        EnrageTimer = 44000 +rand()%3000;
        SoulDrainTimer = 20000 +rand()%5000;
        AuraTimer = 5000;
        checkTimer = 3000;

        emoteDone = false;
        backToCage = false;
        m_creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_HASTE_SPELLS, true);
        m_creature->ApplySpellImmune(1, IMMUNITY_EFFECT, SPELL_EFFECT_INTERRUPT_CAST, true);
    }

    void DamageTaken(Unit *done_by, uint32 &damage)
    {
        if(damage >= m_creature->GetHealth())
        {
            damage = 0;
            backToCage = true;
            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            if(!emoteDone)
            {
                DoScriptText(SUFF_SAY_RECAP, m_creature);
                emoteDone = true;
            }
        }
    }

    void EnterCombat(Unit *who)
    {
        DoZoneInCombat();
        DoScriptText(SUFF_SAY_FREED, m_creature);
        DoCast(m_creature, AURA_OF_SUFFERING, true);
        DoCast(m_creature, ESSENCE_OF_SUFFERING_PASSIVE, true);
        DoCast(m_creature, ESSENCE_OF_SUFFERING_PASSIVE2, true);
    }

    void KilledUnit(Unit *victim)
    {
        if(victim->GetGUID() == FixateVictimGUID)
            FixateTimer = 0;

        DoScriptText(RAND(SUFF_SAY_SLAY1, SUFF_SAY_SLAY2, SUFF_SAY_SLAY3), m_creature);
    }

    void CastFixate()
    {
        std::list<Unit *> targets;
        Map *map = m_creature->GetMap();
        if(map->IsDungeon())
        {
            InstanceMap::PlayerList const &PlayerList = ((InstanceMap*)map)->GetPlayers();
            for (InstanceMap::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
            {
                if (Player* i_pl = i->getSource())
                {
                    if(i_pl && i_pl->isAlive() && !i_pl->isGameMaster())
                        targets.push_back(i_pl);
                }
            }
        }

        if(targets.empty())
            return; // No targets added for some reason. No point continuing.

        targets.sort(TargetDistanceOrder(m_creature)); // Sort players by distance.
        targets.resize(1); // Only need closest target.

        Unit* target = targets.front();
        if(target)
        {
            FixateVictimGUID = target->GetGUID();
            DoResetThreat();

            target->CastSpell(m_creature, SPELL_FIXATE_TAUNT, true);
            m_creature->AddAura(SPELL_FIXATE_TARGET, target);
            m_creature->AddThreat(target, 1000000.0f);
        }
    }

    void UpdateAI(const uint32 diff)
    {
        if(m_creature->isInCombat())
        {
            //Supposed to be cast on nearest target
            if(FixateTimer < diff)
            {
                CastFixate();
                if(urand(0,16) == 0)
                    DoScriptText(SUFF_SAY_AGGRO, m_creature);

                FixateTimer = 5000;
            }
            else
                FixateTimer -= diff;
        }

        //Return since we have no target
        if (!UpdateVictim())
            return;

        if (checkTimer <= diff)
        {
            m_creature->SetSpeed(MOVE_RUN, 2.5);
            checkTimer = 3000;
        }
        else
            checkTimer -= diff;

        if(EnrageTimer < diff)
        {
            DoCast(m_creature, SPELL_ENRAGE, true);
            DoScriptText(SUFF_EMOTE_ENRAGE, m_creature);
            EnrageTimer = 44000 +rand()%3000;
        }
        else
            EnrageTimer -= diff;

        if(SoulDrainTimer < diff)
        {
            DoCast(m_creature, SPELL_SOUL_DRAIN);

            SoulDrainTimer = 20000 +rand()%5000;
        }
        else
            SoulDrainTimer -= diff;

        if(!backToCage)
            DoMeleeAttackIfReady();
    }
};

struct TRINITY_DLL_DECL boss_essence_of_desireAI : public ScriptedAI
{
    boss_essence_of_desireAI(Creature *c) : ScriptedAI(c) {}

    uint32 RuneShieldTimer;
    uint32 DeadenTimer;
    uint32 SoulShockTimer;
    uint32 checkTimer;

    bool emoteDone;
    bool backToCage;

    void Reset()
    {
        RuneShieldTimer = 15000;
        DeadenTimer = 30000;
        SoulShockTimer = 5000;
        checkTimer = 3000;
        m_creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_CONFUSE, true);

        emoteDone = false;
        backToCage = false;
    }

    void DamageTaken(Unit *done_by, uint32 &damage)
    {
        if(damage >= m_creature->GetHealth())
        {
            damage = 0;
            backToCage = true;
            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            if(!emoteDone)
            {
                DoScriptText(DESI_SAY_RECAP, m_creature);
                emoteDone = true;
            }
        }
        else
        {
            int32 bp0 = damage / 2;
            m_creature->CastCustomSpell(done_by, AURA_OF_DESIRE_DAMAGE, &bp0, NULL, NULL, true);
        }
    }

    void SpellHit(Unit *caster, const SpellEntry *spell)
    {
        if(m_creature->m_currentSpells[CURRENT_GENERIC_SPELL])
        {
            for(uint8 i = 0; i < 3; ++i)
            {
                if(spell->Effect[i] == SPELL_EFFECT_INTERRUPT_CAST)
                {
                    SpellEntry const *temp = m_creature->m_currentSpells[CURRENT_GENERIC_SPELL]->m_spellInfo;
                    if(temp->Id == SPELL_SOUL_SHOCK || temp->Id == SPELL_DEADEN)
                    {
                        m_creature->InterruptSpell(CURRENT_GENERIC_SPELL, false);
                        return;
                    }
                }
            }
        }
    }

    void EnterCombat(Unit *who)
    {
        if(m_creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE))
             return;

        DoZoneInCombat();
        DoCast(m_creature, AURA_OF_DESIRE, true);
        DoScriptText(DESI_SAY_FREED, m_creature);
    }

    void KilledUnit(Unit *victim)
    {
        DoScriptText(RAND(DESI_SAY_SLAY1, DESI_SAY_SLAY2, DESI_SAY_SLAY3), m_creature);
    }

    void UpdateAI(const uint32 diff)
    {
        if (!UpdateVictim())
            return;

        if (checkTimer <= diff)
        {
            m_creature->SetSpeed(MOVE_RUN, 2.5);
            checkTimer = 3000;
        }
        else
            checkTimer -= diff;

        if(RuneShieldTimer < diff)
        {
            ForceSpellCast(m_creature, SPELL_RUNE_SHIELD);

            RuneShieldTimer = 15000;
        }
        else
            RuneShieldTimer -= diff;

        if(SoulShockTimer < diff)
        {
            AddSpellToCast(m_creature->getVictim(), SPELL_SOUL_SHOCK);
            SoulShockTimer = 5000;
        }
        else
            SoulShockTimer -= diff;

        if(DeadenTimer < diff)
        {
            if(urand(0,1))
                AddSpellToCastWithScriptText(m_creature->getVictim(), SPELL_DEADEN, DESI_SAY_SPEC);
            else
                AddSpellToCast(m_creature->getVictim(), SPELL_DEADEN);

            DeadenTimer = 30000;
        }
        else
            DeadenTimer -= diff;

        if(!backToCage)
        {
            CastNextSpellIfAnyAndReady();
            DoMeleeAttackIfReady();
        }
    }
};

struct TRINITY_DLL_DECL boss_essence_of_angerAI : public ScriptedAI
{
    boss_essence_of_angerAI(Creature *c) : ScriptedAI(c) {}

    uint64 AggroTargetGUID;

    uint32 CheckTankTimer;
    uint32 SoulScreamTimer;
    uint32 SpiteTimer;

    std::list<uint64> SpiteTargetGUID;

    bool CheckedAggro;

    void Reset()
    {
        AggroTargetGUID = 0;

        CheckTankTimer = 5000;
        SoulScreamTimer = 10000;
        SpiteTimer = 30000;

        SpiteTargetGUID.clear();

        CheckedAggro = false;
        m_creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_HASTE_SPELLS, true);
        m_creature->ApplySpellImmune(1, IMMUNITY_EFFECT, SPELL_EFFECT_INTERRUPT_CAST, true);
    }

    void EnterCombat(Unit *who)
    {
        DoScriptText(RAND(ANGER_SAY_FREED, ANGER_SAY_FREED2), m_creature);

        DoZoneInCombat();
        DoCast(m_creature, AURA_OF_ANGER, true);
    }

    void JustDied(Unit *victim)
    {
        DoScriptText(ANGER_SAY_DEATH, m_creature);
    }

    void KilledUnit(Unit *victim)
    {
        DoScriptText(RAND(ANGER_SAY_SLAY1, ANGER_SAY_SLAY2), m_creature);
    }

    void UpdateAI(const uint32 diff)
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        if(!CheckedAggro)
        {
            AggroTargetGUID = m_creature->getVictimGUID();
            CheckedAggro = true;
        }

        if(CheckTankTimer < diff)
        {
            if(m_creature->getVictimGUID() != AggroTargetGUID)
            {
                DoScriptText(ANGER_SAY_BEFORE, m_creature);
                DoCast(m_creature, SPELL_SELF_SEETHE, true);
                AggroTargetGUID = m_creature->getVictimGUID();
            }

            m_creature->SetSpeed(MOVE_RUN, 2.5);

            CheckTankTimer = 2000;
        }
        else
            CheckTankTimer -= diff;

        if(SoulScreamTimer < diff)
        {
            DoCast(m_creature, SPELL_SOUL_SCREAM);

            if(!urand(0,2))
                DoScriptText(ANGER_SAY_SPEC, m_creature);

            SoulScreamTimer = 10000;
        }
        else
            SoulScreamTimer -= diff;

        if(SpiteTimer < diff)
        {
            DoCast(m_creature, SPELL_SPITE_TARGET);
            DoScriptText(ANGER_SAY_SPEC, m_creature);
            SpiteTimer = 30000;
        }
        else
            SpiteTimer -= diff;

        DoMeleeAttackIfReady();
    }
};


CreatureAI* GetAI_boss_reliquary_of_souls(Creature *_Creature)
{
    return new boss_reliquary_of_soulsAI (_Creature);
}

CreatureAI* GetAI_boss_essence_of_suffering(Creature *_Creature)
{
    return new boss_essence_of_sufferingAI (_Creature);
}

CreatureAI* GetAI_boss_essence_of_desire(Creature *_Creature)
{
    return new boss_essence_of_desireAI (_Creature);
}

CreatureAI* GetAI_boss_essence_of_anger(Creature *_Creature)
{
    return new boss_essence_of_angerAI (_Creature);
}

CreatureAI* GetAI_npc_enslaved_soul(Creature *_Creature)
{
    return new npc_enslaved_soulAI (_Creature);
}

CreatureAI* GetAI_npc_ros_trigger(Creature *_Creature)
{
    return new npc_ros_triggerAI (_Creature);
}

void AddSC_boss_reliquary_of_souls()
{
    Script *newscript;
    newscript = new Script;
    newscript->Name="boss_reliquary_of_souls";
    newscript->GetAI = &GetAI_boss_reliquary_of_souls;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="boss_essence_of_suffering";
    newscript->GetAI = &GetAI_boss_essence_of_suffering;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="boss_essence_of_desire";
    newscript->GetAI = &GetAI_boss_essence_of_desire;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="boss_essence_of_anger";
    newscript->GetAI = &GetAI_boss_essence_of_anger;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_enslaved_soul";
    newscript->GetAI = &GetAI_npc_enslaved_soul;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_ros_trigger";
    newscript->GetAI = &GetAI_npc_ros_trigger;
    newscript->RegisterSelf();
}

