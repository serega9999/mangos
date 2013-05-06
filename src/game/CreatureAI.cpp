/*
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
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

#include "CreatureAI.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Spell.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"

CreatureAI::~CreatureAI()
{
}

void CreatureAI::AttackedBy(Unit* attacker)
{
    if (!m_creature->getVictim())
        AttackStart(attacker);
}

CanCastResult CreatureAI::CanCastSpell(Unit* pTarget, const SpellEntry* pSpell, bool isTriggered)
{
    if (!pTarget)
        return CAST_FAIL_OTHER;

    // If not triggered, we check
    if (!isTriggered)
    {
        // State does not allow
        if (m_creature->hasUnitState(UNIT_STAT_CAN_NOT_REACT_OR_LOST_CONTROL))
            return CAST_FAIL_STATE;

        if (pSpell->PreventionType == SPELL_PREVENTION_TYPE_SILENCE && m_creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED))
            return CAST_FAIL_STATE;

        if (pSpell->PreventionType == SPELL_PREVENTION_TYPE_PACIFY && m_creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED))
            return CAST_FAIL_STATE;

        // Check for power (also done by Spell::CheckCast())
        if ((int32)m_creature->GetPower((Powers)pSpell->powerType) < Spell::CalculatePowerCost(pSpell, m_creature))
            return CAST_FAIL_POWER;

        if (m_creature->HasSpellCooldown(pSpell))
            return CAST_FAIL_SPELL_COOLDOWN;
    }

    if (SpellRangeEntry const* pSpellRange = sSpellRangeStore.LookupEntry(pSpell->rangeIndex))
    {
        if (pTarget != m_creature)
        {
            // pTarget is out of range of this spell (also done by Spell::CheckCast())
            float fDistance = m_creature->GetCombatDistance(pTarget);

            if (fDistance > (m_creature->IsHostileTo(pTarget) ? pSpellRange->maxRange : pSpellRange->maxRangeFriendly))
                return CAST_FAIL_TOO_FAR;

            float fMinRange = m_creature->IsHostileTo(pTarget) ? pSpellRange->minRange : pSpellRange->minRangeFriendly;

            if (fMinRange && fDistance < fMinRange)
                return CAST_FAIL_TOO_CLOSE;
        }

        return CAST_OK;
    }
    else
        return CAST_FAIL_OTHER;
}

CanCastResult CreatureAI::DoCastSpellIfCan(Unit* pTarget, uint32 uiSpell, uint32 uiCastFlags, ObjectGuid uiOriginalCasterGUID)
{
    Unit* pCaster = m_creature;

    if (uiCastFlags & CAST_FORCE_TARGET_SELF)
        pCaster = pTarget;

    if (!pTarget)
        return CAST_FAIL_OTHER;

    // Allowed to cast only if not casting (unless we interrupt ourself) or if spell is triggered
    if (!pCaster->IsNonMeleeSpellCasted(false) || (uiCastFlags & (CAST_TRIGGERED | CAST_INTERRUPT_PREVIOUS)))
    {
        if (const SpellEntry* pSpell = sSpellStore.LookupEntry(uiSpell))
        {
            // If cast flag CAST_AURA_NOT_PRESENT is active, check if target already has aura on them
            if (uiCastFlags & CAST_AURA_NOT_PRESENT)
            {
                if (pTarget->HasAura(uiSpell))
                    return CAST_FAIL_TARGET_AURA;
            }

            // Check if cannot cast spell
            if (!(uiCastFlags & (CAST_FORCE_TARGET_SELF | CAST_FORCE_CAST)))
            {
                CanCastResult castResult = CanCastSpell(pTarget, pSpell, uiCastFlags & CAST_TRIGGERED);

                if (castResult != CAST_OK)
                    return castResult;
            }

            // Interrupt any previous spell
            if ((uiCastFlags & CAST_INTERRUPT_PREVIOUS) && pCaster->IsNonMeleeSpellCasted(false))
                pCaster->InterruptNonMeleeSpells(false);

            pCaster->CastSpell(pTarget, pSpell, uiCastFlags & CAST_TRIGGERED, NULL, NULL, uiOriginalCasterGUID);
            return CAST_OK;
        }
        else
        {
            sLog.outErrorDb("DoCastSpellIfCan by creature entry %u attempt to cast spell %u but spell does not exist.", m_creature->GetEntry(), uiSpell);
            return CAST_FAIL_OTHER;
        }
    }
    else
        return CAST_FAIL_IS_CASTING;
}

bool CreatureAI::AttackByType(WeaponAttackType attType)
{
    // Make sure our attack is ready before checking distance
    if (m_creature->isAttackReady(attType) && m_creature->CanReachWithMeleeAttack(m_creature->getVictim()))
    {
        m_creature->AttackerStateUpdate(m_creature->getVictim(), attType);
        m_creature->resetAttackTimer(attType);
        WeaponAttackType attTypeTwo = (attType == BASE_ATTACK ? OFF_ATTACK : BASE_ATTACK);
        if (m_creature->getAttackTimer(attTypeTwo) < 500)
            m_creature->setAttackTimer(attTypeTwo, 500);
        return true;
    }

    return false;
}

bool CreatureAI::DoMeleeAttackIfReady()
{
    return m_creature->UpdateMeleeAttackingState();
}

void CreatureAI::SetCombatMovement(bool enable, bool stopOrStartMovement /*=false*/)
{
    m_isCombatMovement = enable;

    if (enable)
        m_creature->clearUnitState(UNIT_STAT_NO_COMBAT_MOVEMENT);
    else
        m_creature->addUnitState(UNIT_STAT_NO_COMBAT_MOVEMENT);

    if (stopOrStartMovement && m_creature->getVictim())     // Only change current movement while in combat
    {
        if (enable)
            m_creature->GetMotionMaster()->MoveChase(m_creature->getVictim(), m_attackDistance, m_attackAngle);
        else if (!enable && m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            m_creature->StopMoving();
    }
}

void CreatureAI::HandleMovementOnAttackStart(Unit* victim)
{
    if (m_isCombatMovement)
        m_creature->GetMotionMaster()->MoveChase(victim, m_attackDistance, m_attackAngle);
    else
    {
        m_creature->GetMotionMaster()->MoveIdle();
        m_creature->StopMoving();
    }
}

// ////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Event system
// ////////////////////////////////////////////////////////////////////////////////////////////////

void CreatureAI::SendAIEvent(AIEventType eventType, Unit* pInvoker, uint32 uiDelay, float fRadius) const
{
    if (fRadius > 0)
    {
        std::list<Creature*> receiverList;

        // Use this check here to collect only assitable creatures in case of CALL_ASSISTANCE, else be less strict
        MaNGOS::AnyAssistCreatureInRangeCheck u_check(m_creature, eventType == AI_EVENT_CALL_ASSISTANCE ? pInvoker : NULL, fRadius);
        MaNGOS::CreatureListSearcher<MaNGOS::AnyAssistCreatureInRangeCheck> searcher(receiverList, u_check);
        Cell::VisitGridObjects(m_creature, searcher, fRadius);

        if (!receiverList.empty())
        {
            AiDelayEventAround* e = new AiDelayEventAround(eventType, pInvoker ? pInvoker->GetObjectGuid() : ObjectGuid(), *m_creature, receiverList);
            m_creature->AddEvent(e, uiDelay);
        }
    }
}

void CreatureAI::SendAIEvent(AIEventType eventType, Unit* pInvoker, Creature* pReceiver) const
{
    MANGOS_ASSERT(pReceiver);
    pReceiver->AI()->ReceiveAIEvent(eventType, m_creature, pInvoker);
}
