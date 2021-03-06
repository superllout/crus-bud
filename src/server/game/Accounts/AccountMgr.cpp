/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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

#include "AccountMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Util.h"
#include "SHA1.h"
#include "WorldSession.h"

AccountMgr::AccountMgr()
{
}

AccountMgr::~AccountMgr()
{
    ClearRBAC();
}

AccountOpResult AccountMgr::CreateAccount(std::string username, std::string password, std::string email = "")
{
    if (utf8length(username) > MAX_ACCOUNT_STR)
        return AOR_NAME_TOO_LONG;                           // username's too long

    normalizeString(username);
    normalizeString(password);
    normalizeString(email);

    if (GetId(username))
        return AOR_NAME_ALREADY_EXIST;                       // username does already exist

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_ACCOUNT);

    stmt->setString(0, username);
    stmt->setString(1, CalculateShaPassHash(username, password));
    stmt->setString(2, email);
    stmt->setString(3, email);

    LoginDatabase.DirectExecute(stmt); // Enforce saving, otherwise AddGroup can fail

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_REALM_CHARACTERS_INIT);

    LoginDatabase.Execute(stmt);

    // Add default rbac groups for that security level
    RBACData* rbac = new RBACData(GetId(username), username, -1);
    // No need to Load From DB, as it's new data

    RBACGroupContainer const& groupsToAdd = _defaultSecGroups[0]; // 0: Default sec level
    for (RBACGroupContainer::const_iterator it = groupsToAdd.begin(); it != groupsToAdd.end(); ++it)
        rbac->AddGroup(*it, -1);

    delete rbac;

    return AOR_OK;                                          // everything's fine
}

AccountOpResult AccountMgr::DeleteAccount(uint32 accountId)
{
    // Check if accounts exists
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BY_ID);
    stmt->setUInt32(0, accountId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (!result)
        return AOR_NAME_NOT_EXIST;

    // Obtain accounts characters
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARS_BY_ACCOUNT_ID);

    stmt->setUInt32(0, accountId);

    result = CharacterDatabase.Query(stmt);

    if (result)
    {
        do
        {
            uint32 guidLow = (*result)[0].GetUInt32();
            uint64 guid = MAKE_NEW_GUID(guidLow, 0, HIGHGUID_PLAYER);

            // Kick if player is online
            if (Player* p = ObjectAccessor::FindConnectedPlayer(guid))
            {
                WorldSession* s = p->GetSession();
                s->KickPlayer();                            // mark session to remove at next session list update
                s->LogoutPlayer(false);                     // logout player without waiting next session list update
            }

            Player::DeleteFromDB(guid, accountId, false);       // no need to update realm characters
        } while (result->NextRow());
    }

    // table realm specific but common for all characters of account for realm
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_TUTORIALS);
    stmt->setUInt32(0, accountId);
    CharacterDatabase.Execute(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ACCOUNT_DATA);
    stmt->setUInt32(0, accountId);
    CharacterDatabase.Execute(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHARACTER_BAN);
    stmt->setUInt32(0, accountId);
    CharacterDatabase.Execute(stmt);

    SQLTransaction trans = LoginDatabase.BeginTransaction();

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT);
    stmt->setUInt32(0, accountId);
    trans->Append(stmt);

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT_ACCESS);
    stmt->setUInt32(0, accountId);
    trans->Append(stmt);

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_REALM_CHARACTERS);
    stmt->setUInt32(0, accountId);
    trans->Append(stmt);

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT_BANNED);
    stmt->setUInt32(0, accountId);
    trans->Append(stmt);

    LoginDatabase.CommitTransaction(trans);

    return AOR_OK;
}

AccountOpResult AccountMgr::ChangeUsername(uint32 accountId, std::string newUsername, std::string newPassword)
{
    // Check if accounts exists
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BY_ID);
    stmt->setUInt32(0, accountId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (!result)
        return AOR_NAME_NOT_EXIST;

    if (utf8length(newUsername) > MAX_ACCOUNT_STR)
        return AOR_NAME_TOO_LONG;

    if (utf8length(newPassword) > MAX_ACCOUNT_STR)
        return AOR_PASS_TOO_LONG;

    normalizeString(newUsername);
    normalizeString(newPassword);

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_USERNAME);

    stmt->setString(0, newUsername);
    stmt->setString(1, CalculateShaPassHash(newUsername, newPassword));
    stmt->setUInt32(2, accountId);

    LoginDatabase.Execute(stmt);

    return AOR_OK;
}

AccountOpResult AccountMgr::ChangePassword(uint32 accountId, std::string newPassword)
{
    std::string username;

    if (!GetName(accountId, username))
        return AOR_NAME_NOT_EXIST;                          // account doesn't exist

    if (utf8length(newPassword) > MAX_ACCOUNT_STR)
        return AOR_PASS_TOO_LONG;

    normalizeString(username);
    normalizeString(newPassword);

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_PASSWORD);

    stmt->setString(0, CalculateShaPassHash(username, newPassword));
    stmt->setUInt32(1, accountId);

    LoginDatabase.Execute(stmt);

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_VS);

    stmt->setString(0, "");
    stmt->setString(1, "");
    stmt->setString(2, username);

    LoginDatabase.Execute(stmt);

    return AOR_OK;
}

AccountOpResult AccountMgr::ChangeEmail(uint32 accountId, std::string newEmail)
{
    std::string username;

    if (!GetName(accountId, username))
        return AOR_NAME_NOT_EXIST;                          // account doesn't exist

    if (utf8length(newEmail) > MAX_EMAIL_STR)
        return AOR_EMAIL_TOO_LONG;

    normalizeString(username);
    normalizeString(newEmail);

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_EMAIL);

    stmt->setString(0, newEmail);
    stmt->setUInt32(1, accountId);

    LoginDatabase.Execute(stmt);

    return AOR_OK;
}

AccountOpResult AccountMgr::ChangeRegEmail(uint32 accountId, std::string newEmail)
{
    std::string username;

    if (!GetName(accountId, username))
        return AOR_NAME_NOT_EXIST;                          // account doesn't exist

    if (utf8length(newEmail) > MAX_EMAIL_STR)
        return AOR_EMAIL_TOO_LONG;

    normalizeString(username);
    normalizeString(newEmail);

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_REG_EMAIL);

    stmt->setString(0, newEmail);
    stmt->setUInt32(1, accountId);

    LoginDatabase.Execute(stmt);

    return AOR_OK;
}

uint32 AccountMgr::GetId(std::string const& username)
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_ACCOUNT_ID_BY_USERNAME);
    stmt->setString(0, username);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    return (result) ? (*result)[0].GetUInt32() : 0;
}

uint32 AccountMgr::GetSecurity(uint32 accountId)
{
    for (AccountAccessSet::iterator itr = _accountAccess.begin(); itr != _accountAccess.end(); ++itr)
        if ((*itr)->_accountId == accountId)
            return (*itr)->_gmlevel;
    return uint32(SEC_PLAYER);
}

uint32 AccountMgr::GetSecurity(uint32 accountId, int32 realmId)
{
    for (AccountAccessSet::iterator itr = _accountAccess.begin(); itr != _accountAccess.end(); ++itr)
        if ((*itr)->_accountId == accountId && ((*itr)->_realmId == realmId || (*itr)->_realmId == -1))
            return (*itr)->_gmlevel;
    return uint32(SEC_PLAYER);
}

bool AccountMgr::GetName(uint32 accountId, std::string& name)
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_USERNAME_BY_ID);
    stmt->setUInt32(0, accountId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (result)
    {
        name = (*result)[0].GetString();
        return true;
    }

    return false;
}

bool AccountMgr::GetEmail(uint32 accountId, std::string& email)
{
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_EMAIL_BY_ID);
    stmt->setUInt32(0, accountId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (result)
    {
        email = (*result)[0].GetString();
        return true;
    }

    return false;
}

bool AccountMgr::CheckPassword(uint32 accountId, std::string password)
{
    std::string username;

    if (!GetName(accountId, username))
        return false;

    normalizeString(username);
    normalizeString(password);

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_CHECK_PASSWORD);
    stmt->setUInt32(0, accountId);
    stmt->setString(1, CalculateShaPassHash(username, password));
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    return (result) ? true : false;
}

bool AccountMgr::CheckEmail(uint32 accountId, std::string newEmail)
{
    std::string oldEmail;

    // We simply return false for a non-existing email
    if (!GetEmail(accountId, oldEmail))
        return false;

    normalizeString(oldEmail);
    normalizeString(newEmail);

    if (strcmp(oldEmail.c_str(), newEmail.c_str()) == 0)
        return true;

    return false;
}

uint32 AccountMgr::GetCharactersCount(uint32 accountId)
{
    // check character count
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_SUM_CHARS);
    stmt->setUInt32(0, accountId);
    PreparedQueryResult result = CharacterDatabase.Query(stmt);

    return (result) ? (*result)[0].GetUInt64() : 0;
}

bool AccountMgr::normalizeString(std::string& utf8String)
{
    wchar_t buffer[MAX_ACCOUNT_STR+1];

    size_t maxLength = MAX_ACCOUNT_STR;
    if (!Utf8toWStr(utf8String, buffer, maxLength))
        return false;
#ifdef _MSC_VER
#pragma warning(disable: 4996)
#endif
    std::transform(&buffer[0], buffer+maxLength, &buffer[0], wcharToUpperOnlyLatin);
#ifdef _MSC_VER
#pragma warning(default: 4996)
#endif

    return WStrToUtf8(buffer, maxLength, utf8String);
}

std::string AccountMgr::CalculateShaPassHash(std::string const& name, std::string const& password)
{
    SHA1Hash sha;
    sha.Initialize();
    sha.UpdateData(name);
    sha.UpdateData(":");
    sha.UpdateData(password);
    sha.Finalize();

    return ByteArrayToHexStr(sha.GetDigest(), sha.GetLength());
}

bool AccountMgr::IsPlayerAccount(uint32 gmlevel)
{
    return gmlevel == SEC_PLAYER;
}

bool AccountMgr::IsAdminAccount(uint32 gmlevel)
{
    return gmlevel >= SEC_ADMINISTRATOR && gmlevel <= SEC_CONSOLE;
}

bool AccountMgr::IsConsoleAccount(uint32 gmlevel)
{
    return gmlevel == SEC_CONSOLE;
}

void AccountMgr::LoadRBAC()
{
    ClearRBAC();

    TC_LOG_INFO(LOG_FILTER_RBAC, "AccountMgr::LoadRBAC");
    uint32 oldMSTime = getMSTime();
    uint32 count1 = 0;
    uint32 count2 = 0;
    uint32 count3 = 0;

    TC_LOG_DEBUG(LOG_FILTER_RBAC, "AccountMgr::LoadRBAC: Loading permissions");
    QueryResult result = LoginDatabase.Query("SELECT id, name FROM rbac_permissions");
    if (!result)
    {
        TC_LOG_INFO(LOG_FILTER_SQL, ">> Loaded 0 account permission definitions. DB table `rbac_permissions` is empty.");
        return;
    }

    do
    {
        Field* field = result->Fetch();
        uint32 id = field[0].GetUInt32();
        _permissions[id] = new RBACPermission(id, field[1].GetString());
        ++count1;
    }
    while (result->NextRow());

    TC_LOG_DEBUG(LOG_FILTER_RBAC, "AccountMgr::LoadRBAC: Loading roles");
    result = LoginDatabase.Query("SELECT id, name FROM rbac_roles");
    if (!result)
    {
        TC_LOG_INFO(LOG_FILTER_SQL, ">> Loaded 0 account role definitions. DB table `rbac_roles` is empty.");
        return;
    }

    do
    {
        Field* field = result->Fetch();
        uint32 id = field[0].GetUInt32();
        _roles[id] = new RBACRole(id, field[1].GetString());
        ++count2;
    }
    while (result->NextRow());

    TC_LOG_DEBUG(LOG_FILTER_RBAC, "AccountMgr::LoadRBAC: Loading role permissions");
    result = LoginDatabase.Query("SELECT roleId, permissionId FROM rbac_role_permissions");
    if (!result)
    {
        TC_LOG_INFO(LOG_FILTER_SQL, ">> Loaded 0 account role-permission definitions. DB table `rbac_role_permissions` is empty.");
        return;
    }

    do
    {
        Field* field = result->Fetch();
        uint32 id = field[0].GetUInt32();
        RBACRole* role = _roles[id];
        role->GrantPermission(field[1].GetUInt32());
    }
    while (result->NextRow());

    TC_LOG_DEBUG(LOG_FILTER_RBAC, "AccountMgr::LoadRBAC: Loading groups");
    result = LoginDatabase.Query("SELECT id, name FROM rbac_groups");
    if (!result)
    {
        TC_LOG_INFO(LOG_FILTER_SQL, ">> Loaded 0 account group definitions. DB table `rbac_groups` is empty.");
        return;
    }

    do
    {
        Field* field = result->Fetch();
        uint32 id = field[0].GetUInt32();
        _groups[id] = new RBACGroup(id, field[1].GetString());
        ++count3;
    }
    while (result->NextRow());

    TC_LOG_DEBUG(LOG_FILTER_RBAC, "AccountMgr::LoadRBAC: Loading group roles");
    result = LoginDatabase.Query("SELECT groupId, roleId FROM rbac_group_roles");
    if (!result)
    {
        TC_LOG_INFO(LOG_FILTER_SQL, ">> Loaded 0 account group-role definitions. DB table `rbac_group_roles` is empty.");
        return;
    }

    do
    {
        Field* field = result->Fetch();
        uint32 id = field[0].GetUInt32();
        RBACGroup* group = _groups[id];
        group->GrantRole(field[1].GetUInt32());
    }
    while (result->NextRow());

    TC_LOG_DEBUG(LOG_FILTER_RBAC, "AccountMgr::LoadRBAC: Loading security level groups");
    result = LoginDatabase.Query("SELECT secId, groupId FROM rbac_security_level_groups ORDER by secId ASC");
    if (!result)
    {
        TC_LOG_INFO(LOG_FILTER_SQL, ">> Loaded 0 account default groups for security levels definitions. DB table `rbac_security_level_groups` is empty.");
        return;
    }

    uint8 lastSecId = 255;
    RBACGroupContainer* groups = NULL;
    do
    {
        Field* field = result->Fetch();
        uint8 secId = field[0].GetUInt8();

        if (lastSecId != secId)
            groups = &_defaultSecGroups[secId];

        groups->insert(field[1].GetUInt32());
    }
    while (result->NextRow());

    TC_LOG_INFO(LOG_FILTER_SERVER_LOADING, ">> Loaded %u permission definitions, %u role definitions and %u group definitions in %u ms", count1, count2, count3, GetMSTimeDiffToNow(oldMSTime));

    TC_LOG_DEBUG(LOG_FILTER_RBAC, "AccountMgr::LoadRBAC: Loading default groups");
    // Load default groups to be added to any RBAC Object.
    std::string defaultGroups = sConfigMgr->GetStringDefault("RBAC.DefaultGroups", "");
    Tokenizer tokens(defaultGroups, ',');
    for (Tokenizer::const_iterator itr = tokens.begin(); itr != tokens.end(); ++itr)
        if (uint32 groupId = atoi(*itr))
            _defaultGroups.insert(groupId);
}

void AccountMgr::UpdateAccountAccess(RBACData* rbac, uint32 accountId, uint8 securityLevel, int32 realmId)
{
    int32 serverRealmId = realmId != -1 ? realmId : sConfigMgr->GetIntDefault("RealmID", 0);
    bool needDelete = false;
    if (!rbac)
    {
        needDelete = true;
        rbac = new RBACData(accountId, "", serverRealmId);
        rbac->LoadFromDB();
    }

    // Get max security level and realm (checking current realm and -1)
    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_ACCESS_BY_ID);
    stmt->setUInt32(0, accountId);
    stmt->setInt32(1, serverRealmId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);
    if (result)
    {
        do
        {
            Field* field = result->Fetch();
            uint8 secLevel = field[0].GetUInt8();
            int32 realmId = field[1].GetUInt32();

            RBACGroupContainer const& groupsToRemove = _defaultSecGroups[secLevel];
            for (RBACGroupContainer::const_iterator it = groupsToRemove.begin(); it != groupsToRemove.end(); ++it)
                rbac->RemoveGroup(*it, realmId);
        }
        while (result->NextRow());
    }

    // Add new groups depending on the new security Level
    RBACGroupContainer const& groupsToAdd = _defaultSecGroups[securityLevel];
    for (RBACGroupContainer::const_iterator it = groupsToAdd.begin(); it != groupsToAdd.end(); ++it)
        rbac->AddGroup(*it, realmId);

    if (needDelete)
        delete rbac;

    // Delete old security level from DB
    if (realmId == -1)
    {
        PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT_ACCESS);
        stmt->setUInt32(0, accountId);
        LoginDatabase.Execute(stmt);
    }
    else
    {
        PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_ACCOUNT_ACCESS_BY_REALM);
        stmt->setUInt32(0, accountId);
        stmt->setUInt32(1, realmId);
        LoginDatabase.Execute(stmt);
    }

    // Add new security level
    if (securityLevel)
    {
        PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_ACCOUNT_ACCESS);
        stmt->setUInt32(0, accountId);
        stmt->setUInt8(1, securityLevel);
        stmt->setInt32(2, realmId);
        LoginDatabase.Execute(stmt);
    }
}

RBACGroup const* AccountMgr::GetRBACGroup(uint32 groupId) const
{
    TC_LOG_TRACE(LOG_FILTER_RBAC, "AccountMgr::GetRBACGroup: groupId: %u", groupId);
    RBACGroupsContainer::const_iterator it = _groups.find(groupId);
    if (it != _groups.end())
        return it->second;

    return NULL;
}

RBACRole const* AccountMgr::GetRBACRole(uint32 roleId) const
{
    TC_LOG_TRACE(LOG_FILTER_RBAC, "AccountMgr::GetRBACRole: roleId: %u", roleId);
    RBACRolesContainer::const_iterator it = _roles.find(roleId);
    if (it != _roles.end())
        return it->second;

    return NULL;
}

RBACPermission const* AccountMgr::GetRBACPermission(uint32 permissionId) const
{
    TC_LOG_TRACE(LOG_FILTER_RBAC, "AccountMgr::GetRBACPermission: roleId: %u", permissionId);
    RBACPermissionsContainer::const_iterator it = _permissions.find(permissionId);
    if (it != _permissions.end())
        return it->second;

    return NULL;
}

bool AccountMgr::HasPermission(uint32 accountId, uint32 permissionId, uint32 realmId)
{
    if (!accountId)
    {
        TC_LOG_ERROR(LOG_FILTER_RBAC, "AccountMgr::HasPermission: Wrong accountId 0");
        return false;
    }

    RBACData rbac(accountId, "", realmId);
    rbac.LoadFromDB();
    bool hasPermission = rbac.HasPermission(permissionId);

    TC_LOG_DEBUG(LOG_FILTER_RBAC, "AccountMgr::HasPermission [AccountId: %u, PermissionId: %u, realmId: %d]: %u",
                   accountId, permissionId, realmId, hasPermission);
    return hasPermission;
}

void AccountMgr::ClearRBAC()
{
    for (RBACPermissionsContainer::iterator itr = _permissions.begin(); itr != _permissions.end(); ++itr)
        delete itr->second;

    for (RBACRolesContainer::iterator itr = _roles.begin(); itr != _roles.end(); ++itr)
        delete itr->second;

    for (RBACGroupsContainer::iterator itr = _groups.begin(); itr != _groups.end(); ++itr)
        delete itr->second;

    _permissions.clear();
    _roles.clear();
    _groups.clear();
    _defaultGroups.clear();
    _defaultSecGroups.clear();
}

void AccountMgr::LoadAccountAccess()
{
    _accountAccess.clear();

    uint32 oldMSTime = getMSTime();
    uint32 count = 0;

    QueryResult result = LoginDatabase.PQuery("SELECT id, gmlevel, RealmID FROM account_access WHERE RealmID = %u OR RealmID = -1", realmID);
    if (result)
    {
        do
        {
            Field* field = result->Fetch();
            uint32 id = field[0].GetUInt32();
            uint8 gmlevel = field[1].GetUInt8();
            uint8 realmId = field[2].GetUInt8();

            _accountAccess.insert(new AccountAccess(id, gmlevel, realmId));
            ++count;
        }
        while (result->NextRow());
    }
    else
    {
        TC_LOG_INFO(LOG_FILTER_SQL, ">> Loaded 0 account access. DB table `account_access` is empty.");
        return;
    }

    TC_LOG_INFO(LOG_FILTER_SERVER_LOADING, ">> Loaded %u account access in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}
