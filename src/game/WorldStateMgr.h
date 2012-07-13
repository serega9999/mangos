/*
 * Copyright (C) 2011-2012 /dev/rsa for MangosR2 <http://github.com/mangosR2>
 * 
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

#ifndef MANGOS_WORLD_STATE_MGR_H
#define MANGOS_WORLD_STATE_MGR_H

#include "Common.h"
#include "World.h"

class Player;

enum WorldStateType
{
    WORLD_STATE_TYPE_CUSTOM                     = 0,
    WORLD_STATE_TYPE_WORLD                      = 1,
    WORLD_STATE_TYPE_BGWEEKEND                  = 2,
    WORLD_STATE_TYPE_EVENT                      = 3,
    WORLD_STATE_TYPE_MAP                        = 4,
    WORLD_STATE_TYPE_ZONE                       = 5,
    WORLD_STATE_TYPE_AREA                       = 6,
    WORLD_STATE_TYPE_BATTLEGROUND               = 7,
    WORLD_STATE_TYPE_CAPTURE_POINT              = 8,
    WORLD_STATE_TYPE_WORLD_UNCOMMON             = 9,
    WORLD_STATE_TYPE_MAX
};

enum WorldStateFlags
{
    // 0 byte - dynamic states
    WORLD_STATE_FLAG_INITIALIZED                = 0,
    WORLD_STATE_FLAG_ACTIVE                     = 1,
    WORLD_STATE_FLAG_SAVED                      = 2,
    WORLD_STATE_FLAG_EXPIRED                    = 3,
    WORLD_STATE_FLAG_UPDATED                    = 4,
    WORLD_STATE_FLAG_DELETED                    = 7,
    // 1 byte - dynamic states
    // 2 byte - static states
    WORLD_STATE_FLAG_INITIAL_STATE              = 16,
    WORLD_STATE_FLAG_PASSIVE_AT_CREATE          = 17,
    // 3 byte - custom states
    WORLD_STATE_FLAG_CUSTOM_FORMAT              = 24,
    WORLD_STATE_FLAG_CUSTOM_GLOBAL              = 25,
    WORLD_STATE_FLAG_CUSTOM_HIDDEN              = 26,
    WORLD_STATE_FLAG_CUSTOM                     = 31,
    WORLD_STATE_FLAG_MAX
};

struct WorldStateTemplate
{
    // Constructor for use with DB templates data
    WorldStateTemplate(uint32 _stateid, uint32 _type, uint32 _condition, uint32 _flags, uint32 _default, uint32 _linked)
        : m_stateId(_stateid), m_stateType(_type), m_condition(_condition), m_flags(_flags), m_defaultValue(_default), m_linkedId(_linked)
    {
    };

    public:

    bool IsGlobal() const
    {
        return (m_stateType == WORLD_STATE_TYPE_WORLD || m_stateType == WORLD_STATE_TYPE_BGWEEKEND || m_stateType == WORLD_STATE_TYPE_EVENT);
    }

    uint32             const m_stateId;         // WorldState ID (0 - for BG and some OutDoor PvP)
    uint32             const m_stateType;       // WorldState type
    uint32             const m_condition;       // contains some conditions type (by m_stateType):
                                                // Map Id
                                                // Zone id (0 for world, some instance and BG states)
                                                // Area id (0 for world, some instance and BG states)
    uint32             const m_flags;
    uint32             const m_defaultValue;    // Default value from DB (0 for DBC states)
    uint32             const m_linkedId;        // Linked (uplink) state Id

    bool               HasFlag(WorldStateFlags flag) const { return (m_flags & (1 << flag)); };
};

typedef std::multimap<uint32 /* state id */, WorldStateTemplate>  WorldStateTemplateMap;
typedef std::pair<WorldStateTemplateMap::const_iterator,WorldStateTemplateMap::const_iterator> WorldStateTemplateBounds;

struct WorldState
{
    public:
    // For create new state
    WorldState(WorldStateTemplate const* _state, uint32 _instance) 
        : m_pState(_state), m_instanceId(_instance), m_stateId(m_pState->m_stateId), m_type(m_pState->m_stateType)
    {
        Initialize();
    }

    // For load
    WorldState(WorldStateTemplate const* _state, uint32 _instance, uint32 _flags, uint32 _value, time_t _renewtime) 
        : m_pState(_state), m_instanceId(_instance), m_flags(_flags), m_value(_value), m_renewTime(_renewtime), m_stateId(m_pState->m_stateId), m_type(m_pState->m_stateType)
    {
        m_linkedGuid.Clear();
        m_clientGuids.clear();
        Reload();
    }

    // For load custom state
    WorldState(uint32 _stateid, uint32 _instance, uint32 _flags, uint32 _value, time_t _renewtime) 
        : m_pState(NULL), m_stateId(_stateid), m_instanceId(_instance), m_flags(_flags), m_value(_value), m_renewTime(_renewtime), m_type(WORLD_STATE_TYPE_CUSTOM)
    {
        Initialize();
    }

    // For create new custom state
    WorldState(uint32 _stateid, uint32 _instance, uint32 value) 
        : m_pState(NULL), m_stateId(_stateid), m_instanceId(_instance), m_value(value), m_type(WORLD_STATE_TYPE_CUSTOM)
    {
        Initialize();
    }

    bool IsGlobal()
    {
        return (m_type == WORLD_STATE_TYPE_WORLD || m_type == WORLD_STATE_TYPE_BGWEEKEND || m_type == WORLD_STATE_TYPE_EVENT) ||
            (m_type == WORLD_STATE_TYPE_CUSTOM && HasFlag(WORLD_STATE_FLAG_CUSTOM) && HasFlag(WORLD_STATE_FLAG_CUSTOM_GLOBAL));
    }

    void Initialize()
    {
        m_renewTime = time(NULL);
        m_linkedGuid.Clear();
        m_clientGuids.clear();
        if (m_pState)
        {
            m_condition = m_pState->m_condition;
            m_flags     = m_pState->m_flags;
            m_value     = m_pState->m_defaultValue;
        }
        else
        {
            m_condition = 0;
            m_flags     = 0;
            m_value     = 0;
            AddFlag(WORLD_STATE_FLAG_CUSTOM);
        };
    }

    void Reload()
    {
        if (m_pState)
        {
            m_condition = m_pState->m_condition;
            m_flags     = m_pState->m_flags;
            m_value     = m_pState->m_defaultValue;
        }
        else
        {
            AddFlag(WORLD_STATE_FLAG_CUSTOM);
        }
    }

    // Client operations
    void AddClient(Player* player);
    void AddClient(ObjectGuid const& guid)       { if (guid.IsPlayer()) m_clientGuids.insert(guid);};

    bool HasClients()                      const { return !m_clientGuids.empty();};
    bool HasClient(Player* player)         const;
    bool HasClient(ObjectGuid const& guid) const { return m_clientGuids.find(guid) != m_clientGuids.end();};

    void RemoveClient(Player* player);
    void RemoveClient(ObjectGuid const& guid)    { if (guid.IsPlayer()) m_clientGuids.erase(guid);};

    GuidSet const& GetClients()                  { return m_clientGuids; };

    // Linked operations
    ObjectGuid const& GetLinkedGuid()            { return m_linkedGuid;};
    void              SetLinkedGuid(ObjectGuid const& guid)    { m_linkedGuid = guid;};

    bool IsExpired() const;

    WorldStateTemplate const* GetTemplate() { return m_pState; }

    void AddFlag(WorldStateFlags flag)    { m_flags |= (1 << flag); };
    void RemoveFlag(WorldStateFlags flag) { m_flags &= ~(1 << flag); };
    bool HasFlag(WorldStateFlags flag) const   { return bool(m_flags & (1 << flag)); };

    uint32 const& GetId()        const { return m_stateId; }
    uint32 const& GetCondition() const { return m_condition; }
    uint32 const& GetType()      const { return m_type; }
    uint32 const& GetInstance()  const { return m_instanceId; }
    uint32 const& GetFlags()     const { return m_flags; }
    time_t const& GetRenewTime() const { return m_renewTime; }

    uint32 const& GetValue()     const { return m_value; }
    void          SetValue(uint32 value)
    {
        m_value = value;
        AddFlag(WORLD_STATE_FLAG_UPDATED);
        RemoveFlag(WORLD_STATE_FLAG_SAVED);
        m_renewTime = time(NULL);
    }

    private:
    // const parameters (must be setted in constructor)
    const WorldStateTemplate*          m_pState;        // pointer to template (may be NULL for custom states)
    const uint32                       m_stateId;       // state id  (not unique, 0 for custom and BG states)
    const uint32                       m_instanceId;    // instance Id
    const uint32                       m_type;          // type
    // not-const variables (may be changed later)
    uint32                             m_flags;         // in initialize copyed from template
    uint32                             m_value;         // current value (NU for BG states)
    uint32                             m_condition;     // condition (for custom states)
    time_t                             m_renewTime;     // time of last renew
    ObjectGuid                         m_linkedGuid;    // Guid of GO/creature/etc, which linked to WorldState (CapturePoint mostly)
    GuidSet                            m_clientGuids;   // List of player Guids, wich already received this WorldState update
};

typedef std::multimap<uint32 /* state id */, WorldState>   WorldStateMap;
typedef std::pair<WorldStateMap::const_iterator,WorldStateMap::const_iterator> WorldStateBounds;
typedef std::set<WorldState*> WorldStateSet;

// class MANGOS_DLL_DECL WorldStateMgr : public MaNGOS::Singleton<WorldStateMgr, MaNGOS::ClassLevelLockable<WorldStateMgr, ACE_Thread_Mutex> >
class MANGOS_DLL_DECL WorldStateMgr
{
    public:
        WorldStateMgr() {}

    public:
        void Initialize();

        // Load
        void LoadTemplatesFromDB();
        void LoadTemplatesFromObjectTemplateDB();
        void LoadTemplatesFromDBC();
        void LoadFromDB();

        // Save
        void SaveToDB();
        void Save(WorldState const* state);

        // create state operation
        void CreateWorldStatesIfNeed();
        void CreateZoneAreaStateIfNeed(Player* player, uint32 zone, uint32 area);

        // instance operations
        void CreateInstanceState(Map* map);
        void DeleteInstanceState(uint32 mapId, uint32 instanceId);

        // Methods for digging current states (need remove after finish alfa state)
        void SaveToTemplate(WorldStateType type, uint32 state, uint32 value, uint32 data);

        // Cleanup states
        void Update();

        // In-map operations
        void MapUpdate(Map* map);

        // WorldState operations
        WorldState const* CreateWorldState(WorldStateTemplate const* tmpl, uint32 instanceId, uint32 value = UINT32_MAX);
        WorldState const* CreateWorldState(uint32 stateId, uint32 instanceId, uint32 value = UINT32_MAX);
        WorldState const* GetWorldState(uint32 stateId, uint32 instanceId, uint32 type = WORLD_STATE_TYPE_MAX /*used in special states only*/);
        void DeleteWorldState(WorldState* state);
        WorldStateTemplate const* FindTemplate(uint32 stateId, uint32 type = WORLD_STATE_TYPE_MAX, uint32 condition = 0);

        // External WorldState operations
        uint32 GetWorldStateValue(uint32 stateId);
        uint32 GetWorldStateValueFor(Player* player, uint32 stateId);

        void   SetWorldStateValueFor(Player* player, uint32 stateId, uint32 value);
        void   SetWorldStateValueFor(Map* map, uint32 stateId, uint32 value);

        // Method for initial (per-script) WorldState filling (need move all updates like this in DB!)
        void FillInitialWorldState(uint32 stateId, uint32 value, WorldStateType type, uint32 data = 0 /*zone id*/);

        WorldStateSet GetWorldStates(uint32 flags) { return GetWorldStatesFor(NULL, flags); };
        WorldStateSet GetWorldStatesFor(Player* player, WorldStateFlags flag) { return GetWorldStatesFor(player, (1 << flag)); };
        WorldStateSet GetWorldStatesFor(Player* player, uint32 flags = UINT32_MAX);

        WorldStateSet GetUpdatedWorldStatesFor(Player* player, time_t lastUpdateTime = 0);

        WorldStateSet GetInstanceStates(Map* map, uint32 flags = 0, bool full = false);
        WorldStateSet GetInstanceStates(uint32 mapId, uint32 instanceId, uint32 flags = 0, bool full = false);
        WorldStateSet GetInitWorldStates(uint32 mapId, uint32 instanceId, uint32 zoneId, uint32 areaId);

        bool          IsFitToCondition(Player* player, WorldState const* state);
        bool          IsFitToCondition(Map* map, WorldState const* state);
        bool          IsFitToCondition(uint32 mapId, uint32 instanceId, uint32 zoneId, uint32 areaId, WorldState const* state);

        // Create and manage asynchronous player update lists (NYI)
        void SendWorldState(Player* player, WorldState const* state);
        void RemoveWorldStateFor(Player* player, uint32 stateId);

    private:
        // multithread locking
        typedef   ACE_RW_Thread_Mutex          LockType;
        typedef   ACE_Read_Guard<LockType>     ReadGuard;
        typedef   ACE_Write_Guard<LockType>    WriteGuard;
        LockType& GetLock() { return i_lock; }

        WorldStateTemplateMap   m_worldStateTemplates;    // templates storage
        WorldStateMap           m_worldState;             // data storage
        LockType                i_lock;
};

#define sWorldStateMgr MaNGOS::Singleton<WorldStateMgr>::Instance()
#endif