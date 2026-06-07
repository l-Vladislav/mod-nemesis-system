#include "AllCreatureScript.h"
#include "Chat.h"
#include "CharacterCache.h"
#include "CitySiegeAPI.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Formulas.h"
#include "GameEventMgr.h"
#include "GameTime.h"
#include "Group.h"
#include "GuildMgr.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "TemporarySummon.h"
#include "Mail.h"
#include "Player.h"
#include "Random.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include "UnitScript.h"
#include "WorldPacket.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"

#ifdef MOD_PLAYERBOTS
#include "PlayerbotMgr.h"
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>

using namespace Acore::ChatCommands;

// Early forward declarations — lets functions defined high in the file
// call into namespaces whose full implementation appears much lower.
namespace NemesisBountyBoard
{
    struct ActiveBounty
    {
        uint32 targetSpawnId = 0;
        std::string targetTitle;
        uint32 zoneId     = 0;
        uint32 acceptedAt = 0;
        uint32 expiresAt  = 0;
    };

    bool GetActiveBounty(Player* player, ActiveBounty& out);
}

namespace NemesisReputation
{
    bool   AddPoints(Player* player, uint32 amount);
    uint32 GetPoints(Player* player);
    uint8  GetRank(Player* player);
    void   OnLogin(Player* player);
    void   OnLogout(Player* player);
}

namespace
{
    enum NemesisAffix : uint32
    {
        NEMESIS_AFFIX_VAMPIRIC   = 1 << 0,
        NEMESIS_AFFIX_SWIFT      = 1 << 1,
        NEMESIS_AFFIX_JUGGERNAUT = 1 << 2,
        NEMESIS_AFFIX_SAVAGE     = 1 << 3,
        NEMESIS_AFFIX_SPELLWARD  = 1 << 4,
        NEMESIS_AFFIX_ENRAGED    = 1 << 5,
        NEMESIS_AFFIX_REGEN      = 1 << 6,
    };

    struct NemesisState
    {
        uint32 creatureEntry = 0;
        uint32 mapId = 0;
        uint32 zoneId = 0;
        float homeX = 0.0f;
        float homeY = 0.0f;
        float homeZ = 0.0f;
        uint8 rank = 1;
        uint32 affixMask = 0;
        uint32 baseHealth = 1;
        float baseScale = 1.0f;
        float baseMeleeMinDamage = BASE_MINDAMAGE;
        float baseMeleeMaxDamage = BASE_MAXDAMAGE;
        float baseRangedMinDamage = 0.0f;
        float baseRangedMaxDamage = 0.0f;
        uint32 baseAttackTime = BASE_ATTACK_TIME;
        uint32 baseRangeAttackTime = BASE_ATTACK_TIME;
        float baseRunSpeedRate = 1.0f;
        uint32 targetGuid = 0;
        uint32 lastPromotionAt = 0;
        uint32 lastVictimGuid = 0;
        uint32 createdAt = 0;
        uint32 lastSeenAt = 0;
    };

    struct NemesisAddonView
    {
        ObjectGuid::LowType spawnId = 0;
        uint32 creatureEntry = 0;
        std::string name;
        std::string localizedName;
        uint32 mapId = 0;
        uint32 zoneId = 0;
        std::string zoneName;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float mapX = 0.5f;
        float mapY = 0.5f;
        uint8 level = 0;
        uint8 rank = 1;
        std::string rankTier;
        uint32 affixMask = 0;
        std::string affixText;
        uint32 targetGuid = 0;
        std::string targetName;
        std::string relation;
        std::string rewardClass;
        std::string threatClass;
        uint32 lastSeenAt = 0;
        std::string runtimeGuid;
        std::string nemesisTitle;
        // Player-specific extensions appended to every UPSERT/REMOVE payload
        // so the client's BountyBoard menu can render without an extra fetch.
        uint32 repPoints  = 0;
        uint8  repRank    = 1;
        uint32 expiresAt  = 0;  // active bounty's expiry (0 if not this player's bounty)
    };

    // Nemesis title generator — deterministic from spawnId + creatureEntry
    // Russian nemesis name generator: 60 prefixes x 60 suffixes x 40 titles = 144,000 combinations
    static constexpr char const* NemesisPrefixes[] = {
        "\xD0\x9A\xD1\x80\xD0\xBE\xD0\xB2\xD0\xBE",       // Кровo
        "\xD0\xA2\xD0\xB5\xD0\xBD\xD0\xB5",                 // Тене
        "\xD0\x9C\xD1\x80\xD0\xB0\xD0\xBA\xD0\xBE",         // Мрако
        "\xD0\xA1\xD0\xBC\xD0\xB5\xD1\x80\xD1\x82\xD0\xBE", // Смерто
        "\xD0\x93\xD0\xBD\xD0\xB8\xD0\xBB\xD0\xBE",         // Гнило
        "\xD0\x9A\xD0\xBE\xD1\x81\xD1\x82\xD0\xBE",         // Косто
        "\xD0\x9F\xD0\xBB\xD0\xB0\xD0\xBC\xD0\xB5",         // Пламе
        "\xD0\x9B\xD0\xB5\xD0\xB4\xD0\xBE",                 // Ледо
        "\xD0\xAF\xD0\xB4\xD0\xBE",                         // Ядо
        "\xD0\x93\xD1\x80\xD0\xBE\xD0\xBC\xD0\xBE",         // Громо
        "\xD0\x94\xD1\x8B\xD0\xBC\xD0\xBE",                 // Дымо
        "\xD0\x96\xD0\xB5\xD0\xBB\xD0\xB5\xD0\xB7\xD0\xBE", // Железо
        "\xD0\xA8\xD0\xB8\xD0\xBF\xD0\xBE",                 // Шипо
        "\xD0\x97\xD0\xBB\xD0\xBE",                         // Зло
        "\xD0\xA5\xD0\xBB\xD0\xB0\xD0\xB4\xD0\xBE",         // Хладо
        "\xD0\x93\xD0\xBD\xD0\xB5\xD0\xB2\xD0\xBE",         // Гнево
        "\xD0\xA7\xD1\x83\xD0\xBC\xD0\xBE",                 // Чумо
        "\xD0\x9C\xD0\xBE\xD1\x80\xD0\xBE",                 // Моро
        "\xD0\xA1\xD0\xBA\xD0\xB2\xD0\xB5\xD1\x80",         // Сквер
        "\xD0\x9F\xD0\xB5\xD0\xBF\xD0\xB5\xD0\xBB\xD0\xBE", // Пепело
        "\xD0\x9D\xD0\xBE\xD1\x87\xD0\xB5",                 // Ноче
        "\xD0\xA0\xD0\xB6\xD0\xB0\xD0\xB2\xD0\xBE",         // Ржаво
        "\xD0\x93\xD0\xBD\xD1\x83\xD1\x81\xD0\xBE",         // Гнусо
        "\xD0\xA1\xD0\xBC\xD1\x80\xD0\xB0\xD0\xB4\xD0\xBE", // Смрадо
        "\xD0\x96\xD1\x83\xD1\x82\xD0\xBA\xD0\xBE",         // Жутко
        "\xD0\x9F\xD0\xBE\xD0\xB3\xD0\xB0\xD0\xBD\xD0\xBE", // Погано
        "\xD0\x94\xD1\x80\xD0\xBE\xD0\xB6\xD0\xB5",         // Дроже
        "\xD0\xA1\xD0\xBB\xD0\xB8\xD0\xB7\xD0\xBD\xD0\xBE", // Слизно
        "\xD0\xA5\xD1\x80\xD0\xB8\xD0\xBF\xD0\xBE",         // Хрипо
        "\xD0\x9F\xD1\x80\xD0\xB0\xD1\x85\xD0\xBE",         // Прахо
        "\xD0\x92\xD0\xBE\xD0\xBB\xD1\x87\xD0\xBE",         // Волчо
        "\xD0\x97\xD0\xBC\xD0\xB5\xD0\xB5",                 // Змее
        "\xD0\x9A\xD0\xBE\xD0\xB3\xD1\x82\xD0\xB5",         // Когте
        "\xD0\x9A\xD0\xBB\xD1\x8B\xD0\xBA\xD0\xBE",         // Клыко
        "\xD0\x93\xD0\xBE\xD1\x80\xD0\xB5",                 // Горе
        "\xD0\xA2\xD1\x83\xD1\x85\xD0\xBB\xD0\xBE",         // Тухло
        "\xD0\xA5\xD0\xB0\xD0\xBE\xD1\x81\xD0\xBE",         // Хаосо
        "\xD0\x91\xD0\xB5\xD1\x81\xD0\xBE",                 // Бесо
        "\xD0\x9F\xD1\x80\xD0\xBE\xD0\xBA\xD0\xBB\xD1\x8F\xD1\x82\xD0\xBE", // Проклято
        "\xD0\xA2\xD1\x80\xD1\x83\xD0\xBF\xD0\xBE",         // Трупо
        "\xD0\xA7\xD0\xB5\xD1\x80\xD0\xBD\xD0\xBE",         // Черно
        "\xD0\x94\xD1\x83\xD1\x88\xD0\xB5",                 // Душе
        "\xD0\x97\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBE",         // Зверо
        "\xD0\x9A\xD0\xB0\xD0\xBC\xD0\xBD\xD0\xB5",         // Камне
        "\xD0\xA7\xD0\xB5\xD1\x80\xD0\xB2\xD0\xB5",         // Черве
        "\xD0\xA1\xD1\x82\xD0\xB5\xD1\x80\xD0\xB2\xD0\xBE", // Стерво
        "\xD0\x9A\xD1\x80\xD1\x8B\xD1\x81\xD0\xBE",         // Крысо
        "\xD0\x9F\xD0\xB0\xD1\x83\xD0\xBA\xD0\xBE",         // Пауко
        "\xD0\x93\xD1\x80\xD1\x8F\xD0\xB7\xD0\xB5",         // Грязе
        "\xD0\x9C\xD0\xBE\xD0\xB3\xD0\xB8\xD0\xBB\xD0\xBE", // Могило
        "\xD0\x9E\xD1\x81\xD1\x82\xD1\x80\xD0\xBE",         // Остро
        "\xD0\x9C\xD1\x8F\xD1\x81\xD0\xBE",                 // Мясо
        "\xD0\x96\xD0\xB8\xD0\xBB\xD0\xBE",                 // Жило
        "\xD0\x93\xD1\x80\xD0\xBE\xD0\xB1\xD0\xBE",         // Гробо
        "\xD0\xA1\xD0\xBA\xD0\xBE\xD1\x80\xD0\xB1\xD0\xBE", // Скорбо
        "\xD0\x92\xD0\xBE\xD1\x80\xD0\xBE\xD0\xBD\xD0\xBE", // Вороно
        "\xD0\x9A\xD0\xBE\xD1\x88\xD0\xBC\xD0\xB0\xD1\x80\xD0\xBE", // Кошмаро
        "\xD0\x91\xD1\x80\xD0\xB8\xD1\x82\xD0\xB2\xD0\xBE", // Бритво
        "\xD0\xA3\xD1\x82\xD1\x80\xD0\xBE\xD0\xB1\xD0\xBE", // Утробо
        "\xD0\x9C\xD0\xB5\xD1\x80\xD1\x82\xD0\xB2\xD0\xBE"  // Мертво
    };

    static constexpr char const* NemesisSuffixes[] = {
        "\xD0\xB7\xD1\x83\xD0\xB1",           // зуб
        "\xD0\xBA\xD0\xBE\xD0\xB3\xD0\xBE\xD1\x82\xD1\x8C", // коготь
        "\xD1\x88\xD0\xBA\xD1\x83\xD1\x80",   // шкур
        "\xD0\xBA\xD0\xBB\xD1\x8B\xD0\xBA",   // клык
        "\xD1\x80\xD0\xBE\xD0\xB3",           // рог
        "\xD0\xB3\xD0\xBB\xD0\xB0\xD0\xB7",   // глаз
        "\xD1\x85\xD0\xB2\xD0\xBE\xD1\x81\xD1\x82", // хвост
        "\xD0\xBF\xD0\xB0\xD1\x81\xD1\x82\xD1\x8C", // пасть
        "\xD0\xBB\xD0\xB0\xD0\xBF",           // лап
        "\xD1\x80\xD1\x8B\xD0\xBA",           // рык
        "\xD0\xB2\xD0\xBE\xD0\xB9",           // вой
        "\xD1\x83\xD0\xB4\xD0\xB0\xD1\x80",   // удар
        "\xD0\xB6\xD0\xB0\xD0\xBB\xD0\xBE",   // жало
        "\xD1\x85\xD1\x80\xD0\xB5\xD0\xB1\xD0\xB5\xD1\x82", // хребет
        "\xD0\xBC\xD0\xBE\xD1\x80",           // мор
        "\xD0\xBF\xD0\xBB\xD0\xB5\xD1\x82\xD1\x8C", // плеть
        "\xD0\xB3\xD1\x80\xD1\x8B\xD0\xB7",   // грыз
        "\xD1\x85\xD0\xB2\xD0\xB0\xD1\x82",   // хват
        "\xD0\xB6\xD0\xBE\xD1\x80",           // жор
        "\xD0\xB2\xD0\xB8\xD0\xB7\xD0\xB3",   // визг
        "\xD1\x88\xD0\xB8\xD0\xBF",           // шип
        "\xD1\x80\xD1\x91\xD0\xB2",           // рёв
        "\xD0\xB4\xD1\x80\xD0\xBE\xD0\xB1\xD1\x8C", // дробь
        "\xD1\x82\xD1\x80\xD0\xB5\xD1\x81\xD0\xBA", // треск
        "\xD1\x81\xD0\xBA\xD1\x80\xD0\xB5\xD0\xB6\xD0\xB5\xD1\x82", // скрежет
        "\xD0\xBF\xD0\xBE\xD0\xB6\xD0\xB0\xD1\x80", // пожар
        "\xD1\x82\xD0\xBE\xD0\xBF",           // топ
        "\xD0\xB3\xD1\x80\xD0\xBE\xD1\x85\xD0\xBE\xD1\x82", // грохот
        "\xD0\xBF\xD1\x80\xD1\x8B\xD0\xB6\xD0\xBE\xD0\xBA", // прыжок
        "\xD1\x81\xD0\xBA\xD1\x83\xD0\xBB\xD1\x91\xD0\xB6", // скулёж
        "\xD0\xBC\xD0\xBE\xD1\x80\xD0\xB4",   // морд
        "\xD0\xBF\xD1\x83\xD0\xBA",           // пук
        "\xD0\xBF\xD1\x83\xD0\xB7\xD0\xBE",   // пузо
        "\xD1\x85\xD1\x80\xD1\x83\xD1\x81\xD1\x82", // хруст
        "\xD0\xB1\xD1\x80\xD1\x8B\xD0\xB7\xD0\xB3", // брызг
        "\xD1\x87\xD0\xB0\xD0\xB2\xD0\xBA",   // чавк
        "\xD0\xBA\xD1\x83\xD1\x81",           // кус
        "\xD0\xBF\xD1\x80\xD1\x8B\xD1\x89",   // прыщ
        "\xD0\xBF\xD1\x83\xD0\xBA\xD1\x81",   // пукс
        "\xD1\x88\xD0\xBB\xD1\x91\xD0\xBF",   // шлёп
        "\xD0\xBA\xD1\x80\xD0\xB8\xD0\xBA",   // крик
        "\xD1\x81\xD1\x82\xD0\xBE\xD0\xBD",   // стон
        "\xD0\xBF\xD0\xBB\xD1\x91\xD0\xB2",   // плёв
        "\xD1\x81\xD0\xBB\xD1\x8E\xD0\xBD",   // слюн
        "\xD0\xB4\xD1\x8B\xD1\x85",           // дых
        "\xD0\xB4\xD1\x91\xD1\x80",           // дёр
        "\xD1\x81\xD0\xBA\xD0\xBE\xD0\xBA",   // скок
        "\xD0\xB3\xD0\xBD\xD0\xB8\xD0\xBB\xD1\x8C", // гниль
        "\xD0\xBC\xD0\xBE\xD0\xBB\xD0\xBE\xD1\x82", // молот
        "\xD1\x86\xD0\xB0\xD0\xBF",           // цап
        "\xD0\xB3\xD0\xB0\xD0\xB4",           // гад
        "\xD0\xBB\xD1\x8F\xD0\xB7\xD0\xB3",   // лязг
        "\xD0\xB3\xD0\xBE\xD1\x80\xD0\xB1",   // горб
        "\xD0\xB3\xD0\xB0\xD1\x80\xD1\x8C",   // гарь
        "\xD0\xB3\xD0\xBD\xD0\xBE\xD0\xB9",   // гной
        "\xD0\xB6\xD1\x91\xD0\xBB\xD1\x87\xD1\x8C", // жёлчь
        "\xD1\x85\xD1\x80\xD1\x8F\xD1\x89",   // хрящ
        "\xD0\xBC\xD0\xBE\xD1\x85",           // мох
        "\xD1\x81\xD0\xBA\xD1\x80\xD0\xB8\xD0\xBF", // скрип
        "\xD0\xB3\xD1\x83\xD0\xBB"            // гул
    };

    static constexpr char const* NemesisTitles[] = {
        "\xD0\x9D\xD0\xB5\xD0\xBD\xD0\xB0\xD1\x81\xD1\x8B\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB9", // Ненасытный
        "\xD0\x91\xD0\xB5\xD0\xB7\xD0\xB6\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB9", // Безжалостный
        "\xD0\xA1\xD0\xB2\xD0\xB8\xD1\x80\xD0\xB5\xD0\xBF\xD1\x8B\xD0\xB9", // Свирепый
        "\xD0\x9D\xD0\xB5\xD1\x83\xD0\xBA\xD1\x80\xD0\xBE\xD1\x82\xD0\xB8\xD0\xBC\xD1\x8B\xD0\xB9", // Неукротимый
        "\xD0\x9F\xD1\x80\xD0\xBE\xD0\xBA\xD0\xBB\xD1\x8F\xD1\x82\xD1\x8B\xD0\xB9", // Проклятый
        "\xD0\x96\xD0\xB5\xD1\x81\xD1\x82\xD0\xBE\xD0\xBA\xD0\xB8\xD0\xB9", // Жестокий
        "\xD0\x9A\xD1\x80\xD0\xBE\xD0\xB2\xD0\xBE\xD0\xB6\xD0\xB0\xD0\xB4\xD0\xBD\xD1\x8B\xD0\xB9", // Кровожадный
        "\xD0\xAF\xD1\x80\xD0\xBE\xD1\x81\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB9", // Яростный
        "\xD0\x91\xD0\xB5\xD1\x88\xD0\xB5\xD0\xBD\xD1\x8B\xD0\xB9", // Бешеный
        "\xD0\x96\xD1\x83\xD1\x82\xD0\xBA\xD0\xB8\xD0\xB9", // Жуткий
        "\xD0\x9C\xD0\xB5\xD1\x80\xD0\xB7\xD0\xBA\xD0\xB8\xD0\xB9", // Мерзкий
        "\xD0\x97\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xB5\xD1\x89\xD0\xB8\xD0\xB9", // Зловещий
        "\xD0\x9D\xD0\xB5\xD1\x83\xD0\xB4\xD0\xB5\xD1\x80\xD0\xB6\xD0\xB8\xD0\xBC\xD1\x8B\xD0\xB9", // Неудержимый
        "\xD0\x9E\xD0\xB4\xD0\xB5\xD1\x80\xD0\xB6\xD0\xB8\xD0\xBC\xD1\x8B\xD0\xB9", // Одержимый
        "\xD0\x9B\xD1\x8E\xD1\x82\xD1\x8B\xD0\xB9", // Лютый
        "\xD0\x94\xD0\xB8\xD0\xBA\xD0\xB8\xD0\xB9", // Дикий
        "\xD0\x9A\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80\xD0\xBD\xD1\x8B\xD0\xB9", // Коварный
        "\xD0\xA3\xD0\xB6\xD0\xB0\xD1\x81\xD0\xBD\xD1\x8B\xD0\xB9", // Ужасный
        "\xD0\x91\xD0\xB5\xD1\x81\xD0\xBF\xD0\xBE\xD1\x89\xD0\xB0\xD0\xB4\xD0\xBD\xD1\x8B\xD0\xB9", // Беспощадный
        "\xD0\x92\xD0\xBE\xD0\xBD\xD1\x8E\xD1\x87\xD0\xB8\xD0\xB9", // Вонючий
        "\xD0\x9F\xD1\x83\xD1\x85\xD0\xBB\xD1\x8B\xD0\xB9", // Пухлый
        "\xD0\x9E\xD0\xB1\xD0\xB6\xD0\xBE\xD1\x80\xD0\xB8\xD1\x81\xD1\x82\xD1\x8B\xD0\xB9", // Обжористый
        "\xD0\x97\xD0\xB0\xD0\xBF\xD0\xBB\xD0\xB5\xD1\x81\xD0\xBD\xD0\xB5\xD0\xB2\xD0\xB5\xD0\xBB\xD1\x8B\xD0\xB9", // Заплесневелый
        "\xD0\x9F\xD1\x80\xD0\xBE\xD0\xB6\xD0\xBE\xD1\x80\xD0\xBB\xD0\xB8\xD0\xB2\xD1\x8B\xD0\xB9", // Прожорливый
        "\xD0\x91\xD1\x83\xD0\xB9\xD0\xBD\xD1\x8B\xD0\xB9", // Буйный
        "\xD0\x91\xD0\xB5\xD0\xB7\xD1\x83\xD0\xBC\xD0\xBD\xD1\x8B\xD0\xB9", // Безумный
        "\xD0\x93\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xB4\xD0\xBD\xD1\x8B\xD0\xB9", // Голодный
        "\xD0\x93\xD0\xBD\xD0\xB8\xD0\xBB\xD0\xBE\xD0\xB9", // Гнилой
        "\xD0\x9C\xD1\x80\xD0\xB0\xD1\x87\xD0\xBD\xD1\x8B\xD0\xB9", // Мрачный
        "\xD0\xAF\xD0\xB4\xD0\xBE\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8B\xD0\xB9", // Ядовитый
        "\xD0\x91\xD0\xB5\xD1\x81\xD1\x81\xD0\xBC\xD0\xB5\xD1\x80\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB9", // Бессмертный
        "\xD0\x9E\xD1\x82\xD0\xB2\xD1\x80\xD0\xB0\xD1\x82\xD0\xB8\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C\xD0\xBD\xD1\x8B\xD0\xB9", // Отвратительный
        "\xD0\x93\xD0\xB0\xD0\xB4\xD0\xBA\xD0\xB8\xD0\xB9", // Гадкий
        "\xD0\x9A\xD0\xBE\xD1\x81\xD1\x82\xD0\xBB\xD1\x8F\xD0\xB2\xD1\x8B\xD0\xB9", // Костлявый
        "\xD0\xA5\xD1\x80\xD0\xBE\xD0\xBC\xD0\xBE\xD0\xB9", // Хромой
        "\xD0\xA1\xD0\xBB\xD0\xB5\xD0\xBF\xD0\xBE\xD0\xB9", // Слепой
        "\xD0\xA7\xD1\x83\xD0\xBC\xD0\xBD\xD0\xBE\xD0\xB9", // Чумной
        "\xD0\xA1\xD0\xBC\xD1\x80\xD0\xB0\xD0\xB4\xD0\xBD\xD1\x8B\xD0\xB9", // Смрадный
        "\xD0\x9D\xD0\xB5\xD0\xBD\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x81\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB9", // Ненавистный
        "\xD0\x9A\xD1\x80\xD0\xB8\xD0\xB2\xD0\xBE\xD0\xB9"  // Кривой
    };

    // True if the creature entry is a rare / rare-elite (pool-based respawn).
    // Rare mobs use pool_creature to rotate between spawn points, so multiple
    // spawnIds share one "identity" — we key the nemesis by creatureEntry
    // instead of spawnId so pool rotation doesn't split one rare into many
    // separate nemeses with different titles.
    bool IsRareEntry(uint32 creatureEntry)
    {
        CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(creatureEntry);
        if (!ct)
            return false;
        return ct->rank == CREATURE_ELITE_RARE || ct->rank == CREATURE_ELITE_RAREELITE;
    }

    std::string GenerateNemesisTitle(ObjectGuid::LowType spawnId, uint32 creatureEntry)
    {
        // For rare mobs, seed from entry only so every spawn point of the
        // same rare produces the same generated title. Normal mobs continue
        // to use spawnId XOR entry for per-spawn uniqueness.
        uint32 const seedInput = IsRareEntry(creatureEntry)
            ? creatureEntry
            : uint32(spawnId);
        uint32 const seed = seedInput ^ (creatureEntry * 2654435761u);
        uint32 const prefixCount = sizeof(NemesisPrefixes) / sizeof(NemesisPrefixes[0]);
        uint32 const suffixCount = sizeof(NemesisSuffixes) / sizeof(NemesisSuffixes[0]);
        uint32 const titleCount = sizeof(NemesisTitles) / sizeof(NemesisTitles[0]);
        uint32 const prefixIdx = seed % prefixCount;
        uint32 const suffixIdx = (seed / (prefixCount + 1u)) % suffixCount;
        uint32 const titleIdx = (seed / ((prefixCount + 1u) * (suffixCount + 1u))) % titleCount;

        return Acore::StringFormat("{}{} {}", NemesisPrefixes[prefixIdx], NemesisSuffixes[suffixIdx], NemesisTitles[titleIdx]);
    }

    using NemesisStore = std::unordered_map<ObjectGuid::LowType, NemesisState>;
    using NemesisTickStore = std::unordered_map<ObjectGuid::LowType, uint32>;
    using TemporaryNemesisStore = std::unordered_map<ObjectGuid, NemesisState>;
    using TemporaryNemesisTickStore = std::unordered_map<ObjectGuid, uint32>;

    NemesisStore ActiveNemeses;
    NemesisTickStore RegenTickAccumulators;
    TemporaryNemesisStore ActiveTemporaryNemeses;
    TemporaryNemesisTickStore TemporaryRegenTickAccumulators;
    bool CacheLoaded = false;

    constexpr char NEMESIS_ADDON_PREFIX[] = "Nemesis";
    size_t constexpr NEMESIS_ADDON_CHUNK_SIZE = 450;

    Creature* FindLoadedCreatureBySpawnId(Map* map, ObjectGuid::LowType spawnId);
    std::string GetNemesisDisplayName(Map* map, ObjectGuid::LowType spawnId, NemesisState const& state);
    void EnsureCacheLoaded();
    bool IsExpired(NemesisState const& state);

    bool IsEnabled()
    {
        return sConfigMgr->GetOption<bool>("NemesisSystem.Enable", true);
    }

    bool IsCitySiegeIntegrationEnabled()
    {
        return sConfigMgr->GetOption<bool>("NemesisSystem.CitySiegeIntegration.Enable", false);
    }

    float GetCitySiegePromotionChance()
    {
        return std::clamp(sConfigMgr->GetOption<float>("NemesisSystem.CitySiegeIntegration.Chance", 10.0f), 0.0f, 100.0f);
    }

    uint8 GetMaxRank()
    {
        return std::max<uint8>(1, sConfigMgr->GetOption<uint8>("NemesisSystem.MaxRank", 5));
    }

    uint8 GetMinCreatureLevel()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.MinCreatureLevel", 1);
    }

    uint8 GetMaxCreatureLevel()
    {
        return std::max(GetMinCreatureLevel(), sConfigMgr->GetOption<uint8>("NemesisSystem.MaxCreatureLevel", 255));
    }

    uint8 GetPromotionLevelDiffMax()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.PromotionLevelDiffMax", 5);
    }

    uint8 GetTrivialKillLevelDelta()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.TrivialKillLevelDelta", 5);
    }

    uint8 GetRewardOverlevelDiffMax()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.RewardOverlevelDiffMax", 10);
    }

    uint8 GetRewardUnderlevelDiffMax()
    {
        return sConfigMgr->GetOption<uint8>("NemesisSystem.RewardUnderlevelDiffMax", 10);
    }

    float GetRewardUnderdogMaxMultiplier()
    {
        return std::max(1.0f, sConfigMgr->GetOption<float>("NemesisSystem.RewardUnderdogMaxMultiplier", 2.0f));
    }

    uint32 GetDecayHours()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.DecayHours", 48);
    }

    uint32 GetMaxPerZone()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.MaxPerZone", 20);
    }

    uint32 CountNemesesInZone(uint32 zoneId)
    {
        EnsureCacheLoaded();

        uint32 count = 0;
        for (auto const& [spawnId, state] : ActiveNemeses)
            if (state.zoneId == zoneId)
                ++count;

        return count;
    }

    uint32 GetRankUpCooldownSeconds()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.RankUpCooldownSeconds", 300);
    }

    uint32 GetSameVictimCooldownSeconds()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.SameVictimCooldownSeconds", 900);
    }

    uint32 GetRewardItem(bool revenge)
    {
        return sConfigMgr->GetOption<uint32>(revenge ? "NemesisSystem.RevengeRewardItem" : "NemesisSystem.BountyRewardItem", 0);
    }

    uint32 GetRewardCount(bool revenge)
    {
        return std::max<uint32>(1, sConfigMgr->GetOption<uint32>(revenge ? "NemesisSystem.RevengeRewardCount" : "NemesisSystem.BountyRewardCount", 1));
    }

    uint32 GetRewardGold(bool revenge)
    {
        return sConfigMgr->GetOption<uint32>(revenge ? "NemesisSystem.RevengeRewardGold" : "NemesisSystem.BountyRewardGold", revenge ? 10000 : 2500);
    }

    uint32 GetRewardItemPerRankBonus(bool revenge)
    {
        return sConfigMgr->GetOption<uint32>(revenge ? "NemesisSystem.RevengeRewardItemPerRankBonus" : "NemesisSystem.BountyRewardItemPerRankBonus", 0);
    }

    uint32 GetRewardGoldPerRankBonus(bool revenge)
    {
        return sConfigMgr->GetOption<uint32>(revenge ? "NemesisSystem.RevengeRewardGoldPerRankBonus" : "NemesisSystem.BountyRewardGoldPerRankBonus", revenge ? 2500 : 500);
    }

    bool ShouldAnnounceCreate()
    {
        return sConfigMgr->GetOption<bool>("NemesisSystem.AnnounceOnCreate", true);
    }

    bool ShouldAnnounceKill()
    {
        return sConfigMgr->GetOption<bool>("NemesisSystem.AnnounceOnKill", true);
    }

    uint8 GetAnnounceMinRank()
    {
        return std::max<uint8>(1, sConfigMgr->GetOption<uint8>("NemesisSystem.AnnounceMinRank", 1));
    }

    uint32 GetVisualAuraSpell()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.VisualAuraSpell", 0);
    }

    uint32 GetAddonBootstrapRecentHours()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.AddonBootstrapRecentHours", 24);
    }

    uint32 GetAddonBootstrapMaxEntries()
    {
        return std::max<uint32>(1, sConfigMgr->GetOption<uint32>("NemesisSystem.AddonBootstrapMaxEntries", 100));
    }

    uint32 GetAddonReportCooldownSeconds()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.AddonReportCooldownSeconds", 30);
    }

    bool IsPlayerbotVictim(Player* player)
    {
#ifdef MOD_PLAYERBOTS
        return player && sPlayerbotsMgr.GetPlayerbotAI(player) != nullptr;
#else
        (void)player;
        return false;
#endif
    }

    CitySiegeAPI::SiegeParticipantRole GetCitySiegeRole(Creature const* creature)
    {
        if (!creature)
            return CitySiegeAPI::SiegeParticipantRole::None;

        return CitySiegeAPI::GetActiveCreatureRole(creature->GetGUID());
    }

    bool IsTemporaryNemesisCandidate(Creature const* creature)
    {
        return creature && !creature->GetSpawnId() && GetCitySiegeRole(creature) != CitySiegeAPI::SiegeParticipantRole::None;
    }

    bool HasAffix(NemesisState const& state, NemesisAffix affix)
    {
        return (state.affixMask & affix) != 0;
    }

    bool IsAllowedCreatureRank(uint32 rank)
    {
        switch (rank)
        {
            case CREATURE_ELITE_NORMAL:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowNormal", true);
            case CREATURE_ELITE_ELITE:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowElite", true);
            case CREATURE_ELITE_RARE:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowRare", true);
            case CREATURE_ELITE_RAREELITE:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowRareElite", true);
            case CREATURE_ELITE_WORLDBOSS:
                return sConfigMgr->GetOption<bool>("NemesisSystem.AllowWorldBoss", false);
            default:
                return false;
        }
    }

    float GetScaleMultiplier(uint8 rank)
    {
        switch (rank)
        {
            case 1: return 1.20f;
            case 2: return 1.30f;
            case 3: return 1.40f;
            case 4: return 1.50f;
            default: return 1.60f;
        }
    }

    float GetHealthMultiplier(uint8 rank)
    {
        switch (rank)
        {
            case 1: return 1.50f;
            case 2: return 2.00f;
            case 3: return 3.00f;
            case 4: return 4.50f;
            default: return 6.00f;
        }
    }

    float GetDamageMultiplier(uint8 rank)
    {
        switch (rank)
        {
            case 1: return 1.25f;
            case 2: return 1.50f;
            case 3: return 1.75f;
            case 4: return 2.00f;
            default: return 2.50f;
        }
    }

    float GetVampiricHealPct()
    {
        return 0.50f;
    }

    float GetSwiftSpeedMultiplier()
    {
        return 1.50f;
    }

    float GetSwiftAttackTimeMultiplier()
    {
        return 0.70f;
    }

    float GetSavageDamageMultiplier()
    {
        return 1.25f;
    }

    float GetSpellwardDamageMultiplier()
    {
        return 0.70f;
    }

    float GetEnragedHealthPctThreshold()
    {
        return std::clamp(sConfigMgr->GetOption<float>("NemesisSystem.EnragedHealthPctThreshold", 30.0f), 1.0f, 99.0f);
    }

    float GetEnragedDamageMultiplier()
    {
        return std::max(1.0f, sConfigMgr->GetOption<float>("NemesisSystem.EnragedDamageMultiplier", 1.50f));
    }

    uint32 GetRegenerationIntervalMs()
    {
        return std::max<uint32>(1000, sConfigMgr->GetOption<uint32>("NemesisSystem.RegenerationIntervalMs", 5000));
    }

    float GetRegenerationHealthPct()
    {
        return std::clamp(sConfigMgr->GetOption<float>("NemesisSystem.RegenerationHealthPct", 3.0f), 0.1f, 100.0f);
    }

    std::string GetAffixList(uint32 affixMask)
    {
        std::ostringstream stream;
        bool first = true;

        auto append = [&](char const* name)
        {
            if (!first)
                stream << ", ";

            stream << name;
            first = false;
        };

        if (affixMask & NEMESIS_AFFIX_VAMPIRIC)
            append("Vampiric");

        if (affixMask & NEMESIS_AFFIX_SWIFT)
            append("Swift");

        if (affixMask & NEMESIS_AFFIX_JUGGERNAUT)
            append("Juggernaut");

        if (affixMask & NEMESIS_AFFIX_SAVAGE)
            append("Savage");

        if (affixMask & NEMESIS_AFFIX_SPELLWARD)
            append("Spellward");

        if (affixMask & NEMESIS_AFFIX_ENRAGED)
            append("Enraged");

        if (affixMask & NEMESIS_AFFIX_REGEN)
            append("Regenerating");

        if (first)
            return "None";

        return stream.str();
    }

    std::string SanitizeAddonField(std::string value)
    {
        std::replace(value.begin(), value.end(), ':', ';');
        std::replace(value.begin(), value.end(), '|', '/');
        std::replace(value.begin(), value.end(), '\t', ' ');
        std::replace(value.begin(), value.end(), '\r', ' ');
        std::replace(value.begin(), value.end(), '\n', ' ');
        return value;
    }

    float RoundToNearest(float value, float nearest)
    {
        if (nearest <= 0.0f)
            return value;

        return std::round(value / nearest) * nearest;
    }

    float RoundToDecimals(float value, uint32 decimals)
    {
        float scale = std::pow(10.0f, float(decimals));
        if (scale <= 0.0f)
            return value;

        return std::round(value * scale) / scale;
    }

    std::string GetRankTierLabel(uint8 rank)
    {
        switch (rank)
        {
            case 1: return "Marked";
            case 2: return "Hated";
            case 3: return "Relentless";
            case 4: return "Legendary";
            default: return "Mythic";
        }
    }

    std::string GetLocalizedCreatureName(Player* player, uint32 entry)
    {
        CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(entry);
        if (!ct)
            return "";

        std::string name = ct->Name;
        if (player && player->GetSession())
        {
            LocaleConstant loc_idx = player->GetSession()->GetSessionDbLocaleIndex();
            if (loc_idx >= 0)
                if (CreatureLocale const* cl = sObjectMgr->GetCreatureLocale(entry))
                    ObjectMgr::GetLocaleString(cl->Name, loc_idx, name);
        }
        return name;
    }

    std::string GetZoneName(uint32 zoneId)
    {
        if (!zoneId)
            return "Unknown";

        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId))
            return area->area_name[0] ? area->area_name[0] : "Unknown";

        return "Unknown";
    }

    std::string GetPlayerNameByGuidLow(uint32 guidLow)
    {
        if (!guidLow)
            return "";

        if (Player* target = HashMapHolder<Player>::Find(ObjectGuid::Create<HighGuid::Player>(guidLow)))
            return target->GetName();

        if (CharacterCacheEntry const* characterInfo = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(guidLow)))
            return characterInfo->Name;

        return Acore::StringFormat("Player{}", guidLow);
    }

    std::string BuildAddonEnvelope(std::string const& payload)
    {
        return std::string(NEMESIS_ADDON_PREFIX) + "\t" + payload;
    }

    void SendAddonPayload(Player* player, std::string const& payload)
    {
        if (!player || !player->GetSession())
            return;

        std::string const fullMessage = BuildAddonEnvelope(payload);

        WorldPacket data(SMSG_MESSAGECHAT, 100);
        data << uint8(ChatMsg::CHAT_MSG_WHISPER);
        data << int32(LANG_ADDON);
        data << player->GetGUID();
        data << uint32(0);
        data << player->GetGUID();
        data << uint32(fullMessage.length() + 1);
        data << fullMessage;
        data << uint8(0);

        player->GetSession()->SendPacket(&data);
    }

    void SendChunkedAddonPayload(Player* player, std::string const& payload)
    {
        if (payload.length() <= NEMESIS_ADDON_CHUNK_SIZE)
        {
            SendAddonPayload(player, payload);
            return;
        }

        static uint32 chunkCounter = 0;
        std::string id = std::to_string(++chunkCounter) + "_" + std::to_string(player->GetGUID().GetCounter());
        std::vector<std::string> chunks;
        size_t offset = 0;

        while (offset < payload.size())
        {
            size_t length = std::min(NEMESIS_ADDON_CHUNK_SIZE, payload.size() - offset);
            chunks.push_back(payload.substr(offset, length));
            offset += length;
        }

        for (size_t index = 0; index < chunks.size(); ++index)
        {
            SendAddonPayload(player, Acore::StringFormat("V2:CHUNK:{}:{}:{}:{}", id, index + 1, chunks.size(), chunks[index]));
        }
    }

    std::string GetRelationForPlayer(Player* player, NemesisState const& state)
    {
        if (!player)
            return "public";

        uint32 const playerGuid = player->GetGUID().GetCounter();
        if (state.targetGuid == playerGuid)
            return "own";

        if (Group* group = player->GetGroup())
            if (group->IsMember(ObjectGuid::Create<HighGuid::Player>(state.targetGuid)))
                return "party";

        if (player->GetGuildId())
            if (Player* target = HashMapHolder<Player>::Find(ObjectGuid::Create<HighGuid::Player>(state.targetGuid)))
                if (target->GetGuildId() == player->GetGuildId())
                    return "guild";

        return "public";
    }

    std::string GetRewardClassForPlayer(Player* player, NemesisState const& state)
    {
        if (!player)
            return "none";

        if (player->GetGUID().GetCounter() == state.targetGuid)
            return "revenge";

        if (Group* group = player->GetGroup())
            if (group->IsMember(ObjectGuid::Create<HighGuid::Player>(state.targetGuid)))
                return "shared";

        return "bounty";
    }

    std::string GetThreatClassForPlayer(Player* player, NemesisState const& state, uint8 creatureLevel)
    {
        uint32 score = state.rank;
        if (std::popcount(state.affixMask) >= 2)
            ++score;

        if (player)
        {
            int32 const levelDiff = int32(creatureLevel) - int32(player->GetLevel());
            if (levelDiff >= 5)
                score += 2;
            else if (levelDiff >= 2)
                ++score;
        }

        if (score <= 2)
            return "low";
        if (score <= 4)
            return "medium";
        if (score <= 6)
            return "high";

        return "extreme";
    }

    NemesisAddonView BuildAddonView(Player* player, ObjectGuid::LowType spawnId, NemesisState const& state)
    {
        NemesisAddonView view;
        view.spawnId = spawnId;
        view.creatureEntry = state.creatureEntry;
        view.nemesisTitle = SanitizeAddonField(GenerateNemesisTitle(spawnId, state.creatureEntry));
        view.localizedName = SanitizeAddonField(GetLocalizedCreatureName(player, state.creatureEntry));
        view.mapId = state.mapId;
        view.zoneId = state.zoneId;
        view.zoneName = SanitizeAddonField(GetZoneName(state.zoneId));
        view.x = state.homeX;
        view.y = state.homeY;
        view.z = state.homeZ;
        view.rank = state.rank;
        view.rankTier = GetRankTierLabel(state.rank);
        view.affixMask = state.affixMask;
        view.affixText = SanitizeAddonField(GetAffixList(state.affixMask));
        view.targetGuid = state.targetGuid;
        view.targetName = SanitizeAddonField(GetPlayerNameByGuidLow(state.targetGuid));
        view.relation = GetRelationForPlayer(player, state);
        view.rewardClass = GetRewardClassForPlayer(player, state);
        view.lastSeenAt = state.lastSeenAt ? state.lastSeenAt : state.createdAt;

        Map* searchMap = player ? player->GetMap() : nullptr;
        if (searchMap && searchMap->GetId() != state.mapId)
            searchMap = sMapMgr->FindBaseNonInstanceMap(state.mapId);

        if (Creature* liveCreature = FindLoadedCreatureBySpawnId(searchMap, spawnId))
        {
            if (CreatureTemplate const* ct = liveCreature->GetCreatureTemplate())
                view.name = SanitizeAddonField(ct->Name);
            else
                view.name = SanitizeAddonField(liveCreature->GetName());
            view.zoneId = liveCreature->GetZoneId();
            view.zoneName = SanitizeAddonField(GetZoneName(view.zoneId));
            view.x = liveCreature->GetPositionX();
            view.y = liveCreature->GetPositionY();
            view.z = liveCreature->GetPositionZ();
            view.level = liveCreature->GetLevel();
            view.lastSeenAt = uint32(GameTime::GetGameTime().count());
            view.threatClass = GetThreatClassForPlayer(player, state, view.level);
            view.runtimeGuid = Acore::StringFormat("0x{:016X}", liveCreature->GetGUID().GetRawValue());
        }
        else
        {
            view.name = SanitizeAddonField(GetNemesisDisplayName(nullptr, spawnId, state));
            if (CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(state.creatureEntry))
                view.level = creatureTemplate->maxlevel;
            view.threatClass = GetThreatClassForPlayer(player, state, view.level);
        }

        if (view.zoneId != 0)
        {
            float normalizedX = view.x;
            float normalizedY = view.y;
            Map2ZoneCoordinates(normalizedX, normalizedY, view.zoneId);
            view.mapX = std::clamp(normalizedX / 100.0f, 0.0f, 1.0f);
            view.mapY = std::clamp(normalizedY / 100.0f, 0.0f, 1.0f);
        }

        // Per-player sidecar: rep totals + the bounty expiry timestamp, when
        // this nemesis is the player's current bounty target. Included in
        // every payload so the client doesn't need a separate fetch.
        if (player)
        {
            view.repPoints = NemesisReputation::GetPoints(player);
            view.repRank   = NemesisReputation::GetRank(player);

            NemesisBountyBoard::ActiveBounty active;
            if (NemesisBountyBoard::GetActiveBounty(player, active)
                && active.targetSpawnId == spawnId)
            {
                view.expiresAt = active.expiresAt;
            }
        }

        return view;
    }

    std::string BuildAddonEntryPayload(char const* opcode, NemesisAddonView const& view)
    {
        return Acore::StringFormat(
            "V2:{}:{}:{}:{}:{}:{}:{}:{:.1f}:{:.1f}:{:.1f}:{:.2f}:{:.2f}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
            opcode,
            uint64(view.spawnId),
            view.creatureEntry,
            view.name,
            view.mapId,
            view.zoneId,
            view.zoneName,
            RoundToNearest(view.x, 5.0f),
            RoundToNearest(view.y, 5.0f),
            RoundToNearest(view.z, 5.0f),
            RoundToDecimals(view.mapX, 2),
            RoundToDecimals(view.mapY, 2),
            uint32(view.level),
            uint32(view.rank),
            view.rankTier,
            view.affixMask,
            view.affixText,
            view.targetGuid,
            view.targetName,
            view.relation,
            view.rewardClass,
            view.threatClass,
            view.lastSeenAt,
            view.runtimeGuid,
            view.nemesisTitle,
            view.localizedName,
            view.repPoints,
            uint32(view.repRank),
            view.expiresAt);
    }

    std::string BuildHelloPayload(uint32 entryCount)
    {
        return Acore::StringFormat(
            "V2:HELLO:bootstrap|report|rank5:{}:{}",
            entryCount,
            uint32(GameTime::GetGameTime().count()));
    }

    std::string BuildRemovePayload(ObjectGuid::LowType spawnId, char const* reason)
    {
        return Acore::StringFormat("V2:REMOVE:{}:{}", uint64(spawnId), reason);
    }

    bool ShouldIncludeNemesisInBootstrap(Player* player, NemesisState const& state)
    {
        if (state.rank >= 5)
            return true;

        if (GetRelationForPlayer(player, state) != "public")
            return true;

        uint32 const recentHours = GetAddonBootstrapRecentHours();
        if (!recentHours)
            return false;

        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const lastSeenAt = state.lastSeenAt ? state.lastSeenAt : state.createdAt;
        return lastSeenAt && (lastSeenAt + (recentHours * 60u * 60u) >= now);
    }

    std::vector<ObjectGuid::LowType> CollectBootstrapSpawnIds(Player* player, bool includeAll = false)
    {
        EnsureCacheLoaded();

        struct BootstrapEntry
        {
            ObjectGuid::LowType spawnId = 0;
            uint8 rank = 1;
            uint32 lastSeenAt = 0;
        };

        std::vector<BootstrapEntry> matches;
        matches.reserve(ActiveNemeses.size());

        for (NemesisStore::iterator itr = ActiveNemeses.begin(); itr != ActiveNemeses.end();)
        {
            if (IsExpired(itr->second))
            {
                CharacterDatabase.Execute("DELETE FROM `character_nemesis` WHERE `guid` = {}", uint64(itr->first));
                itr = ActiveNemeses.erase(itr);
                continue;
            }

            if (includeAll || ShouldIncludeNemesisInBootstrap(player, itr->second))
                matches.push_back({ itr->first, itr->second.rank, itr->second.lastSeenAt ? itr->second.lastSeenAt : itr->second.createdAt });

            ++itr;
        }

        std::sort(matches.begin(), matches.end(), [](BootstrapEntry const& left, BootstrapEntry const& right)
        {
            if (left.lastSeenAt != right.lastSeenAt)
                return left.lastSeenAt > right.lastSeenAt;

            if (left.rank != right.rank)
                return left.rank > right.rank;

            return left.spawnId < right.spawnId;
        });

        if (!includeAll && matches.size() > GetAddonBootstrapMaxEntries())
            matches.resize(GetAddonBootstrapMaxEntries());

        std::vector<ObjectGuid::LowType> spawnIds;
        spawnIds.reserve(matches.size());
        for (BootstrapEntry const& entry : matches)
            spawnIds.push_back(entry.spawnId);

        return spawnIds;
    }

    void ForEachOnlinePlayer(std::function<void(Player*)> const& callback)
    {
        WorldSessionMgr::SessionMap const& sessionMap = sWorldSessionMgr->GetAllSessions();
        for (WorldSessionMgr::SessionMap::const_iterator itr = sessionMap.begin(); itr != sessionMap.end(); ++itr)
            if (Player* player = itr->second->GetPlayer())
                callback(player);
    }

    void SendNemesisBootstrap(Player* player, bool includeAll = false)
    {
        if (!player)
            return;

        std::vector<ObjectGuid::LowType> const spawnIds = CollectBootstrapSpawnIds(player, includeAll);

        SendAddonPayload(player, BuildHelloPayload(spawnIds.size()));
        SendAddonPayload(player, Acore::StringFormat("V2:BOOTSTRAP_BEGIN:{}:{}", spawnIds.size(), uint32(GameTime::GetGameTime().count())));

        for (ObjectGuid::LowType spawnId : spawnIds)
        {
            NemesisStore::const_iterator itr = ActiveNemeses.find(spawnId);
            if (itr == ActiveNemeses.end())
                continue;

            NemesisAddonView const view = BuildAddonView(player, spawnId, itr->second);
            SendChunkedAddonPayload(player, BuildAddonEntryPayload("BOOTSTRAP_ENTRY", view));
        }

        SendAddonPayload(player, "V2:BOOTSTRAP_END");
    }

    void SendValidatedNemesisUpsert(Player* player, ObjectGuid::LowType spawnId, NemesisState const& state)
    {
        if (!player)
            return;

        NemesisAddonView const view = BuildAddonView(player, spawnId, state);
        SendChunkedAddonPayload(player, BuildAddonEntryPayload("UPSERT_VALIDATED", view));
    }

    // Sends the player's last 30 bounty completions to the client. Triggered
    // by the ".nemesis addon history" command (invoked from the BountyBoard
    // menu's История tab). Stream shape parallels BOOTSTRAP_*.
    void SendNemesisBountyHistory(Player* player)
    {
        if (!player)
            return;

        uint32 const guidLow = player->GetGUID().GetCounter();
        QueryResult result = CharacterDatabase.Query(
            "SELECT `target_spawn_id`, `target_title`, `target_rank`, "
            "`completed_at`, `tokens_earned` "
            "FROM `character_nemesis_bounty_history` "
            "WHERE `guid` = {} ORDER BY `completed_at` DESC LIMIT 30",
            guidLow);

        uint32 const count = result ? uint32(result->GetRowCount()) : 0;
        SendAddonPayload(player,
            Acore::StringFormat("V2:HIST_BEGIN:{}", count));

        if (result)
        {
            do
            {
                Field* f = result->Fetch();
                uint32 const spawnId = f[0].Get<uint32>();
                std::string const title = SanitizeAddonField(f[1].Get<std::string>());
                uint8  const rank = f[2].Get<uint8>();
                uint32 const completedAt = f[3].Get<uint32>();
                uint32 const tokens = f[4].Get<uint32>();

                SendChunkedAddonPayload(player,
                    Acore::StringFormat("V2:HIST_ENTRY:{}:{}:{}:{}:{}",
                        spawnId, title, uint32(rank), completedAt, tokens));
            } while (result->NextRow());
        }

        SendAddonPayload(player, "V2:HIST_END");
    }

    void BroadcastRankFiveNemesis(ObjectGuid::LowType spawnId, NemesisState const& state)
    {
        if (state.rank < 5)
            return;

        ForEachOnlinePlayer([&](Player* player)
        {
            NemesisAddonView const view = BuildAddonView(player, spawnId, state);
            SendChunkedAddonPayload(player, BuildAddonEntryPayload("RANK5_BROADCAST", view));
        });
    }

    void BroadcastNemesisRemove(ObjectGuid::LowType spawnId, char const* reason)
    {
        ForEachOnlinePlayer([&](Player* player)
        {
            SendAddonPayload(player, BuildRemovePayload(spawnId, reason));
        });
    }

    std::string GetNemesisCoordinates(Creature const* creature)
    {
        if (!creature)
            return "unknown";

        return Acore::StringFormat("{:.1f}, {:.1f}, {:.1f}", creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ());
    }

    void BroadcastNemesisMessage(Creature* /*creature*/, std::string const& message, bool /*serverWide*/ = false)
    {
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, message);
    }

    Creature* FindLoadedCreatureBySpawnId(Map* map, ObjectGuid::LowType spawnId)
    {
        if (!map || !spawnId)
            return nullptr;

        auto bounds = map->GetCreatureBySpawnIdStore().equal_range(spawnId);
        if (bounds.first == bounds.second)
            return nullptr;

        return bounds.first->second;
    }

    std::string GetNemesisDisplayName(Map* map, ObjectGuid::LowType spawnId, NemesisState const& state)
    {
        if (Creature* liveCreature = FindLoadedCreatureBySpawnId(map, spawnId))
            return liveCreature->GetName();

        if (CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(state.creatureEntry))
            return creatureTemplate->Name;

        return Acore::StringFormat("entry {}", state.creatureEntry);
    }

    bool IsExpired(NemesisState const& state)
    {
        uint32 const decayHours = GetDecayHours();
        if (!decayHours || !state.createdAt)
            return false;

        return state.createdAt + (decayHours * 60u * 60u) < uint32(GameTime::GetGameTime().count());
    }

    uint32 GetRankUpCooldownRemaining(NemesisState const& state)
    {
        uint32 const cooldown = GetRankUpCooldownSeconds();
        if (!cooldown)
            return 0;

        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const expiresAt = state.lastPromotionAt + cooldown;

        if (!state.lastPromotionAt || expiresAt <= now)
            return 0;

        return expiresAt - now;
    }

    uint32 GetSameVictimCooldownRemaining(NemesisState const& state, uint32 victimGuid)
    {
        uint32 const cooldown = GetSameVictimCooldownSeconds();
        if (!cooldown)
            return 0;

        if (state.lastVictimGuid != victimGuid)
            return 0;

        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const expiresAt = state.lastPromotionAt + cooldown;

        if (!state.lastPromotionAt || expiresAt <= now)
            return 0;

        return expiresAt - now;
    }

    void EnsureCacheLoaded()
    {
        if (CacheLoaded)
            return;

        CacheLoaded = true;

        QueryResult result = CharacterDatabase.Query(
            "SELECT `guid`, `creature_entry`, `map_id`, `zone_id`, `pos_x`, `pos_y`, `pos_z`, `rank`, `affix_mask`, `base_health`, `base_scale`, `base_melee_min_damage`, "
            "`base_melee_max_damage`, `base_ranged_min_damage`, `base_ranged_max_damage`, `base_attack_time`, `base_range_attack_time`, `base_run_speed_rate`, `nemesis_target_guid`, `last_promotion_at`, `last_victim_guid`, "
            "`last_seen_at`, UNIX_TIMESTAMP(`creation_date`) FROM `character_nemesis`");
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();

            ObjectGuid::LowType const spawnId = fields[0].Get<uint64>();

            NemesisState state;
            state.creatureEntry = fields[1].Get<uint32>();
            state.mapId = fields[2].Get<uint32>();
            state.zoneId = fields[3].Get<uint32>();
            state.homeX = fields[4].Get<float>();
            state.homeY = fields[5].Get<float>();
            state.homeZ = fields[6].Get<float>();
            state.rank = fields[7].Get<uint8>();
            state.affixMask = fields[8].Get<uint32>();
            state.baseHealth = fields[9].Get<uint32>();
            state.baseScale = fields[10].Get<float>();
            state.baseMeleeMinDamage = fields[11].Get<float>();
            state.baseMeleeMaxDamage = fields[12].Get<float>();
            state.baseRangedMinDamage = fields[13].Get<float>();
            state.baseRangedMaxDamage = fields[14].Get<float>();
            state.baseAttackTime = fields[15].Get<uint32>();
            state.baseRangeAttackTime = fields[16].Get<uint32>();
            state.baseRunSpeedRate = fields[17].Get<float>();
            state.targetGuid = fields[18].Get<uint32>();
            state.lastPromotionAt = fields[19].Get<uint32>();
            state.lastVictimGuid = fields[20].Get<uint32>();
            state.lastSeenAt = fields[21].Get<uint32>();
            state.createdAt = fields[22].Get<uint32>();

            if (!state.lastSeenAt)
                state.lastSeenAt = state.createdAt;

            if (IsExpired(state))
            {
                CharacterDatabase.Execute("DELETE FROM `character_nemesis` WHERE `guid` = {}", uint64(spawnId));
                continue;
            }

            ActiveNemeses[spawnId] = state;
        }
        while (result->NextRow());
    }

    bool TryGetNemesisState(ObjectGuid::LowType spawnId, NemesisState& state)
    {
        if (!spawnId)
            return false;

        EnsureCacheLoaded();

        NemesisStore::iterator itr = ActiveNemeses.find(spawnId);
        if (itr == ActiveNemeses.end())
            return false;

        if (IsExpired(itr->second))
        {
            CharacterDatabase.Execute("DELETE FROM `character_nemesis` WHERE `guid` = {}", uint64(spawnId));
            ActiveNemeses.erase(itr);
            return false;
        }

        state = itr->second;
        return true;
    }

    // Finds an existing rare-mob nemesis row for (entry, targetGuid). Returns
    // the stored spawnId key if found, 0 otherwise. Used to dedup rare-mob
    // promotions so pool rotation doesn't create multiple rows per player.
    ObjectGuid::LowType FindRareNemesisSpawnId(uint32 creatureEntry, uint32 targetGuid)
    {
        if (!creatureEntry || !IsRareEntry(creatureEntry))
            return 0;

        EnsureCacheLoaded();

        for (auto const& [spawnId, state] : ActiveNemeses)
        {
            if (state.creatureEntry == creatureEntry
                && state.targetGuid == targetGuid
                && !IsExpired(state))
                return spawnId;
        }
        return 0;
    }

    // Finds any rare-mob nemesis row matching this entry (any target). Used
    // at pool-rotation time to pick up existing nemesis stats when a new
    // spawn point of the rare becomes active. Returns the one with highest
    // rank (most "interesting" state wins if multiple players share it).
    ObjectGuid::LowType FindAnyRareNemesisSpawnId(uint32 creatureEntry)
    {
        if (!creatureEntry || !IsRareEntry(creatureEntry))
            return 0;

        EnsureCacheLoaded();

        ObjectGuid::LowType bestSpawn = 0;
        uint8 bestRank = 0;
        for (auto const& [spawnId, state] : ActiveNemeses)
        {
            if (state.creatureEntry == creatureEntry
                && !IsExpired(state)
                && state.rank > bestRank)
            {
                bestSpawn = spawnId;
                bestRank = state.rank;
            }
        }
        return bestSpawn;
    }

    bool TryGetNemesisState(Creature* creature, NemesisState& state)
    {
        if (!creature)
            return false;

        if (ObjectGuid::LowType const spawnId = creature->GetSpawnId())
            return TryGetNemesisState(spawnId, state);

        TemporaryNemesisStore::iterator itr = ActiveTemporaryNemeses.find(creature->GetGUID());
        if (itr == ActiveTemporaryNemeses.end())
            return false;

        state = itr->second;
        return true;
    }

    // Save a nemesis state. Normally the DB key is creature->GetSpawnId(),
    // but for rare-mob dedup the caller can pass overrideSpawnId to keep
    // writing to the original row even when the currently-active pool spawn
    // has a different spawnId.
    void SaveNemesisState(Creature* creature, NemesisState const& state,
                          ObjectGuid::LowType overrideSpawnId = 0)
    {
        if (!creature)
            return;

        if (!creature->GetSpawnId() && !overrideSpawnId)
        {
            NemesisState storedState = state;
            storedState.homeX = creature->GetPositionX();
            storedState.homeY = creature->GetPositionY();
            storedState.homeZ = creature->GetPositionZ();
            storedState.zoneId = creature->GetZoneId();
            storedState.lastSeenAt = storedState.lastSeenAt ? storedState.lastSeenAt : uint32(GameTime::GetGameTime().count());
            ActiveTemporaryNemeses[creature->GetGUID()] = storedState;
            return;
        }

        EnsureCacheLoaded();

        ObjectGuid::LowType const saveKey = overrideSpawnId ? overrideSpawnId : creature->GetSpawnId();

        NemesisState storedState = state;
        float const homeX = creature->GetPositionX();
        float const homeY = creature->GetPositionY();
        float const homeZ = creature->GetPositionZ();
        storedState.homeX = homeX;
        storedState.homeY = homeY;
        storedState.homeZ = homeZ;
        storedState.zoneId = creature->GetZoneId();
        storedState.lastSeenAt = storedState.lastSeenAt ? storedState.lastSeenAt : uint32(GameTime::GetGameTime().count());

        CharacterDatabase.Execute(
            "REPLACE INTO `character_nemesis` "
            "(`guid`, `creature_entry`, `map_id`, `zone_id`, `pos_x`, `pos_y`, `pos_z`, `rank`, `affix_mask`, `base_health`, `base_scale`, "
            "`base_melee_min_damage`, `base_melee_max_damage`, `base_ranged_min_damage`, `base_ranged_max_damage`, `base_attack_time`, `base_range_attack_time`, `base_run_speed_rate`, `nemesis_target_guid`, `last_promotion_at`, `last_victim_guid`, `creation_date`, `last_seen_at`) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, FROM_UNIXTIME({}), {})",
            uint64(saveKey),
            creature->GetEntry(),
            creature->GetMapId(),
            storedState.zoneId,
            homeX,
            homeY,
            homeZ,
            storedState.rank,
            storedState.affixMask,
            storedState.baseHealth,
            storedState.baseScale,
            storedState.baseMeleeMinDamage,
            storedState.baseMeleeMaxDamage,
            storedState.baseRangedMinDamage,
            storedState.baseRangedMaxDamage,
            storedState.baseAttackTime,
            storedState.baseRangeAttackTime,
            storedState.baseRunSpeedRate,
            storedState.targetGuid,
            storedState.lastPromotionAt,
            storedState.lastVictimGuid,
            storedState.createdAt ? storedState.createdAt : uint32(GameTime::GetGameTime().count()),
            storedState.lastSeenAt);

        ActiveNemeses[saveKey] = storedState;
    }

    // Forward-declared here so DeleteNemesisState can notify the bounty
    // board before a nemesis row is wiped. Full impl in NemesisBountyBoard
    // namespace below.
} // anon ns close — reopened after the forward decl
namespace NemesisBountyBoard
{
    void RefundBountiesForTarget(ObjectGuid::LowType targetSpawnId);
    void InvalidateAllPools();
    void InvalidatePool(Player* player);
}
namespace  // reopen anon ns
{
    void DeleteNemesisState(ObjectGuid::LowType spawnId, char const* reason = "cleared")
    {
        if (!spawnId)
            return;

        EnsureCacheLoaded();

        // Notify the bounty board so any players holding a contract on this
        // target receive a consolation refund before the row disappears.
        NemesisBountyBoard::RefundBountiesForTarget(spawnId);

        ActiveNemeses.erase(spawnId);
        RegenTickAccumulators.erase(spawnId);
        CharacterDatabase.Execute("DELETE FROM `character_nemesis` WHERE `guid` = {}", uint64(spawnId));
        BroadcastNemesisRemove(spawnId, reason);
    }

    void DeleteNemesisState(Creature* creature, char const* reason = "cleared")
    {
        if (!creature)
            return;

        if (ObjectGuid::LowType const spawnId = creature->GetSpawnId())
        {
            DeleteNemesisState(spawnId, reason);
            return;
        }

        ActiveTemporaryNemeses.erase(creature->GetGUID());
        TemporaryRegenTickAccumulators.erase(creature->GetGUID());
        (void)reason;
    }

    void EraseRegenAccumulator(Creature* creature)
    {
        if (!creature)
            return;

        if (ObjectGuid::LowType const spawnId = creature->GetSpawnId())
            RegenTickAccumulators.erase(spawnId);
        else
            TemporaryRegenTickAccumulators.erase(creature->GetGUID());
    }

    uint32& GetRegenAccumulator(Creature* creature)
    {
        if (ObjectGuid::LowType const spawnId = creature->GetSpawnId())
            return RegenTickAccumulators[spawnId];

        return TemporaryRegenTickAccumulators[creature->GetGUID()];
    }

    void BroadcastRankFiveNemesisIfPersistent(Creature* creature, NemesisState const& state)
    {
        if (!creature || !creature->GetSpawnId())
            return;

        BroadcastRankFiveNemesis(creature->GetSpawnId(), state);
    }

    NemesisState BuildInitialNemesisState(Creature* killer, Player* killed)
    {
        NemesisState state;
        state.creatureEntry = killer->GetEntry();
        state.mapId = killer->GetMapId();
        state.zoneId = killer->GetZoneId();
        state.rank = 1;
        state.affixMask = 0;
        state.homeX = killer->GetPositionX();
        state.homeY = killer->GetPositionY();
        state.homeZ = killer->GetPositionZ();
        state.baseHealth = std::max<uint32>(1, killer->GetCreateHealth());
        state.baseScale = killer->GetNativeObjectScale();
        state.baseMeleeMinDamage = std::max<float>(BASE_MINDAMAGE, killer->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE, 0));
        state.baseMeleeMaxDamage = std::max<float>(BASE_MAXDAMAGE, killer->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE, 0));
        state.baseRangedMinDamage = std::max<float>(0.0f, killer->GetWeaponDamageRange(RANGED_ATTACK, MINDAMAGE, 0));
        state.baseRangedMaxDamage = std::max<float>(0.0f, killer->GetWeaponDamageRange(RANGED_ATTACK, MAXDAMAGE, 0));
        state.baseAttackTime = killer->GetCreatureTemplate()->BaseAttackTime;
        state.baseRangeAttackTime = killer->GetCreatureTemplate()->RangeAttackTime;
        state.baseRunSpeedRate = killer->GetSpeedRate(MOVE_RUN);
        state.targetGuid = killed->GetGUID().GetCounter();
        state.createdAt = uint32(GameTime::GetGameTime().count());
        state.lastSeenAt = state.createdAt;
        return state;
    }

    void RollAffixes(NemesisState& state)
    {
        std::array<uint32, 7> const affixes = { NEMESIS_AFFIX_VAMPIRIC, NEMESIS_AFFIX_SWIFT, NEMESIS_AFFIX_JUGGERNAUT, NEMESIS_AFFIX_SAVAGE, NEMESIS_AFFIX_SPELLWARD, NEMESIS_AFFIX_ENRAGED, NEMESIS_AFFIX_REGEN };
        uint32 affixMask = state.affixMask;
        uint32 const desiredAffixCount = state.rank >= 5 ? 3u : (state.rank >= 3 ? 2u : 1u);

        while (std::popcount(affixMask) < desiredAffixCount)
            affixMask |= affixes[urand(0, affixes.size() - 1)];

        state.affixMask = affixMask;
    }

    void ApplyJuggernautImmunity(Creature* creature, bool apply)
    {
        uint32 const placeholderId = 0;

        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_SNARE, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_ROOT, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_FEAR, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_STUN, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_SLEEP, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_CHARM, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_SAPPED, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_POLYMORPH, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_DISORIENTED, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_FREEZE, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_HORROR, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_MECHANIC, MECHANIC_BANISH, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, apply);
        creature->ApplySpellImmune(placeholderId, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK_DEST, apply);
    }

    void ResetCreatureToBaseState(Creature* creature, NemesisState const& state)
    {
        if (!creature)
            return;

        creature->SetObjectScale(state.baseScale);
        creature->SetCreateHealth(state.baseHealth);
        creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(state.baseHealth));
        creature->SetMaxHealth(state.baseHealth);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, state.baseMeleeMinDamage, 0);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, state.baseMeleeMaxDamage, 0);
        creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, state.baseMeleeMinDamage, 0);
        creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, state.baseMeleeMaxDamage, 0);
        creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, state.baseRangedMinDamage, 0);
        creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, state.baseRangedMaxDamage, 0);
        creature->SetAttackTime(BASE_ATTACK, state.baseAttackTime);
        creature->SetAttackTime(OFF_ATTACK, state.baseAttackTime);
        creature->SetAttackTime(RANGED_ATTACK, state.baseRangeAttackTime);
        creature->SetSpeedRate(MOVE_RUN, state.baseRunSpeedRate);
        creature->UpdateSpeed(MOVE_RUN, true);
        ApplyJuggernautImmunity(creature, false);

        if (uint32 auraSpell = GetVisualAuraSpell())
            creature->RemoveAurasDueToSpell(auraSpell);

        // Restore original creature name
        if (CreatureTemplate const* creatureTemplate = creature->GetCreatureTemplate())
            creature->SetName(creatureTemplate->Name);

        creature->UpdateAllStats();

        if (creature->IsAlive())
            creature->SetFullHealth();
    }

    bool IsRevengeKill(Player* killer, NemesisState const& state)
    {
        if (!killer)
            return false;

        if (killer->GetGUID().GetCounter() == state.targetGuid)
            return true;

        Group* group = killer->GetGroup();
        if (!group)
            return false;

        return group->IsMember(ObjectGuid::Create<HighGuid::Player>(state.targetGuid));
    }

    struct RewardRecipients
    {
        std::vector<Player*> players;
        uint8 highestLevel = 0;
    };

    RewardRecipients CollectRewardRecipients(Player* killer, Creature* killed)
    {
        RewardRecipients recipients;
        if (!killer || !killed)
            return recipients;

        auto addRecipient = [&](Player* player)
        {
            if (!player)
                return;

            recipients.players.push_back(player);
            recipients.highestLevel = std::max<uint8>(recipients.highestLevel, player->GetLevel());
        };

        Group* group = killer->GetGroup();
        if (!group)
        {
            addRecipient(killer);
            return recipients;
        }

        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;

            if (member != killer)
            {
                if (member->HasCorpse())
                    continue;

                if (!member->IsAtGroupRewardDistance(killed))
                    continue;
            }

            addRecipient(member);
        }

        if (recipients.players.empty())
            addRecipient(killer);

        return recipients;
    }

    float GetRewardMultiplier(uint8 creatureLevel, uint8 referenceLevel)
    {
        int32 const levelDiff = int32(referenceLevel) - int32(creatureLevel);
        if (levelDiff > 0)
        {
            uint8 const maxDiff = GetRewardOverlevelDiffMax();
            if (!maxDiff)
                return 0.0f;

            float const multiplier = 1.0f - (float(levelDiff) / float(maxDiff));
            return std::clamp(multiplier, 0.0f, 1.0f);
        }

        if (levelDiff < 0)
        {
            uint8 const maxDiff = GetRewardUnderlevelDiffMax();
            float const maxMultiplier = GetRewardUnderdogMaxMultiplier();
            if (!maxDiff || maxMultiplier <= 1.0f)
                return 1.0f;

            uint32 const underlevelDiff = uint32(-levelDiff);
            float const progress = float(std::min<uint32>(underlevelDiff, maxDiff)) / float(maxDiff);
            return 1.0f + ((maxMultiplier - 1.0f) * progress);
        }

        return 1.0f;
    }

    uint32 GetScaledItemCount(uint32 baseCount, float multiplier)
    {
        if (!baseCount || multiplier <= 0.0f)
            return 0;

        float const scaledCount = float(baseCount) * multiplier;
        uint32 scaledItems = uint32(scaledCount);
        float const fractional = scaledCount - float(scaledItems);

        if (fractional > 0.0f && frand(0.0f, 1.0f) < fractional)
            ++scaledItems;

        return scaledItems;
    }

    // Scale gold by creature level: level 1 = 0.05x, level 40 = 0.5x, level 80 = 1.0x
    float GetLevelMultiplier(uint8 creatureLevel)
    {
        return std::clamp(float(creatureLevel) / 80.0f, 0.05f, 1.0f);
    }

    uint32 GetScaledGold(uint32 baseGold, float multiplier)
    {
        if (!baseGold || multiplier <= 0.0f)
            return 0;

        return uint32((float(baseGold) * multiplier) + 0.5f);
    }

    uint32 GetBonusDropItem()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.BonusDrop.Item", 0);
    }

    float GetBonusDropChancePerRank()
    {
        return sConfigMgr->GetOption<float>("NemesisSystem.BonusDrop.ChancePerRank", 35.0f);
    }

    uint32 GetBonusDropCount()
    {
        return sConfigMgr->GetOption<uint32>("NemesisSystem.BonusDrop.Count", 1);
    }

    void AddItemOrMail(Player* player, uint32 itemId, uint32 count)
    {
        if (!player || !itemId || !count)
            return;

        if (player->AddItem(itemId, count))
            return;

        // Inventory full — send via mail
        if (Item* item = Item::CreateItem(itemId, count, player))
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            item->SaveToDB(trans);
            MailDraft("[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0] Reward", "Your bags were full. Here are your nemesis rewards.")
                .AddItem(item)
                .SendMailTo(trans,
                    MailReceiver(player, player->GetGUID().GetCounter()),
                    MailSender(MAIL_CREATURE, 0),
                    MAIL_CHECK_MASK_NONE, 0);
            CharacterDatabase.CommitTransaction(trans);
            ChatHandler(player->GetSession()).PSendSysMessage("Nemesis reward mailed — your bags were full.");
        }
    }

    void GrantReward(Player* player, bool revenge, uint8 rank, float rewardMultiplier, uint8 creatureLevel)
    {
        if (!player)
            return;

        uint32 const rankBonusSteps = rank > 0 ? uint32(rank - 1) : 0;

        // Token reward: guaranteed if creature is not gray (green or higher)
        // No tokens at all if the creature is gray (trivial)
        // Gray creatures give no item rewards at all (tokens or bonus drops)
        bool const isGray = creatureLevel <= Acore::XP::GetGrayLevel(player->GetLevel());
        if (!isGray)
        {
            // Token reward: guaranteed for green or higher
            uint32 const itemCount = GetRewardCount(revenge) + (GetRewardItemPerRankBonus(revenge) * rankBonusSteps);
            if (uint32 itemId = GetRewardItem(revenge); itemId && itemCount)
                AddItemOrMail(player, itemId, itemCount);

            // Bonus drop: chance = ChancePerRank * rank (e.g. 20% per rank → rank 1=20%, rank 5=100%)
            if (uint32 bonusItem = GetBonusDropItem(); bonusItem)
            {
                float const dropChance = std::min(GetBonusDropChancePerRank() * float(rank), 100.0f);
                if (roll_chance_f(dropChance))
                    AddItemOrMail(player, bonusItem, GetBonusDropCount());
            }
        }

        // Gold still scales by level difference and creature level
        uint32 const baseGold = GetRewardGold(revenge) + (GetRewardGoldPerRankBonus(revenge) * rankBonusSteps);
        float const levelMult = GetLevelMultiplier(creatureLevel);
        uint32 const gold = GetScaledGold(baseGold, rewardMultiplier * levelMult);

        if (gold)
            player->ModifyMoney(int32(gold), true);
    }

    bool IsEligibleNemesisKill(Creature* killer, Player* killed)
    {
        if (!IsEnabled() || !killer || !killed)
            return false;

        if (GetCitySiegeRole(killer) != CitySiegeAPI::SiegeParticipantRole::None)
        {
            if (!IsCitySiegeIntegrationEnabled() || !killer->IsInWorld() || IsPlayerbotVictim(killed))
                return false;

            NemesisState state;
            if (TryGetNemesisState(killer, state))
            {
                if (GetRankUpCooldownRemaining(state) > 0)
                    return false;

                if (GetSameVictimCooldownRemaining(state, killed->GetGUID().GetCounter()) > 0)
                    return false;
            }

            float const chance = GetCitySiegePromotionChance();
            return chance > 0.0f && (chance >= 100.0f || roll_chance_f(chance));
        }

        if (!killer->IsInWorld() || !killer->GetSpawnId())
            return false;

        Map* map = killer->GetMap();
        if (!map || map->IsDungeon() || map->IsBattlegroundOrArena() || map->IsRaid())
            return false;

        if (killed->IsInSanctuary())
            return false;

        if (killer->IsPet() || killer->IsCritter())
            return false;

        if (killer->GetLevel() < GetMinCreatureLevel() || killer->GetLevel() > GetMaxCreatureLevel())
            return false;

        int32 levelDiff = int32(killer->GetLevel()) - int32(killed->GetLevel());
        if (levelDiff < 0)
            levelDiff = -levelDiff;

        if (levelDiff > GetPromotionLevelDiffMax())
            return false;

        if (!IsAllowedCreatureRank(killer->GetCreatureTemplate()->rank))
            return false;

        if (killer->IsDungeonBoss())
            return false;

        if (killer->isWorldBoss() && !sConfigMgr->GetOption<bool>("NemesisSystem.AllowWorldBoss", false))
            return false;

        if ((killer->GetLevel() + GetTrivialKillLevelDelta()) < killed->GetLevel())
            return false;

        NemesisState state;
        bool const alreadyNemesis = TryGetNemesisState(killer->GetSpawnId(), state);

        if (alreadyNemesis)
        {
            if (GetRankUpCooldownRemaining(state) > 0)
                return false;

            if (GetSameVictimCooldownRemaining(state, killed->GetGUID().GetCounter()) > 0)
                return false;
        }
        else
        {
            uint32 const maxPerZone = GetMaxPerZone();
            if (maxPerZone && CountNemesesInZone(killer->GetZoneId()) >= maxPerZone)
                return false;
        }

        return true;
    }

    void ApplyNemesisState(Creature* creature, NemesisState const& state)
    {
        if (!creature)
            return;

        uint32 const scaledHealth = std::max<uint32>(1, uint32(float(state.baseHealth) * GetHealthMultiplier(state.rank)));
        float const meleeMinDamage = std::max<float>(BASE_MINDAMAGE, state.baseMeleeMinDamage * GetDamageMultiplier(state.rank));
        float const meleeMaxDamage = std::max<float>(BASE_MAXDAMAGE, state.baseMeleeMaxDamage * GetDamageMultiplier(state.rank));
        float const rangedMinDamage = std::max<float>(0.0f, state.baseRangedMinDamage * GetDamageMultiplier(state.rank));
        float const rangedMaxDamage = std::max<float>(0.0f, state.baseRangedMaxDamage * GetDamageMultiplier(state.rank));

        creature->SetObjectScale(state.baseScale * GetScaleMultiplier(state.rank));
        creature->SetCreateHealth(scaledHealth);
        creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(scaledHealth));
        creature->SetMaxHealth(scaledHealth);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, meleeMinDamage, 0);
        creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, meleeMaxDamage, 0);
        creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, meleeMinDamage, 0);
        creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, meleeMaxDamage, 0);
        creature->SetAttackTime(BASE_ATTACK, state.baseAttackTime);
        creature->SetAttackTime(OFF_ATTACK, state.baseAttackTime);
        creature->SetAttackTime(RANGED_ATTACK, state.baseRangeAttackTime);

        if (state.baseRangedMinDamage > 0.0f || state.baseRangedMaxDamage > 0.0f)
        {
            creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, rangedMinDamage, 0);
            creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, rangedMaxDamage, 0);
        }

        creature->SetSpeedRate(MOVE_RUN, state.baseRunSpeedRate);
        creature->UpdateSpeed(MOVE_RUN, true);

        if (HasAffix(state, NEMESIS_AFFIX_SWIFT))
        {
            creature->SetSpeedRate(MOVE_RUN, state.baseRunSpeedRate * GetSwiftSpeedMultiplier());
            creature->UpdateSpeed(MOVE_RUN, true);
            creature->SetAttackTime(BASE_ATTACK, uint32(float(state.baseAttackTime) * GetSwiftAttackTimeMultiplier()));
            creature->SetAttackTime(OFF_ATTACK, uint32(float(state.baseAttackTime) * GetSwiftAttackTimeMultiplier()));
            creature->SetAttackTime(RANGED_ATTACK, uint32(float(state.baseRangeAttackTime) * GetSwiftAttackTimeMultiplier()));
        }

        if (HasAffix(state, NEMESIS_AFFIX_JUGGERNAUT))
            ApplyJuggernautImmunity(creature, true);

        creature->UpdateAllStats();
        creature->SetMaxHealth(scaledHealth);

        if (creature->IsAlive())
            creature->SetHealth(scaledHealth);
        else
            creature->SetHealth(std::min<uint32>(creature->GetHealth(), scaledHealth));

        if (uint32 auraSpell = GetVisualAuraSpell())
            if (!creature->HasAura(auraSpell))
                creature->AddAura(auraSpell, creature);

        // Set unique Russian nemesis name on the creature
        if (creature->GetSpawnId())
        {
            std::string const title = GenerateNemesisTitle(creature->GetSpawnId(), state.creatureEntry);
            if (!title.empty())
                creature->SetName(title);
        }
    }

    bool IsBelowEnrageThreshold(Creature* creature)
    {
        if (!creature || !creature->GetMaxHealth())
            return false;

        float const healthPct = (100.0f * float(creature->GetHealth())) / float(creature->GetMaxHealth());
        return healthPct <= GetEnragedHealthPctThreshold();
    }

    void PromoteNemesis(Creature* killer, Player* killed)
    {
        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const entry = killer->GetEntry();
        uint32 const targetGuidLow = killed->GetGUID().GetCounter();
        bool const isRare = IsRareEntry(entry);

        // For rare mobs, look up any existing nemesis for this (entry, target)
        // regardless of which pool spawn point we're hitting. This collapses
        // pool rotation into a single persistent nemesis.
        ObjectGuid::LowType saveSpawnId = killer->GetSpawnId();
        NemesisState state;
        bool existed = false;

        if (isRare)
        {
            ObjectGuid::LowType const existingRareKey = FindRareNemesisSpawnId(entry, targetGuidLow);
            if (existingRareKey && TryGetNemesisState(existingRareKey, state))
            {
                existed = true;
                saveSpawnId = existingRareKey;  // keep original row key
            }
        }

        if (!existed)
            existed = TryGetNemesisState(killer, state);

        uint8 const previousRank = state.rank;

        if (existed)
        {
            if (state.rank < GetMaxRank())
                ++state.rank;
        }
        else
            state = BuildInitialNemesisState(killer, killed);

        state.creatureEntry = entry;
        state.mapId = killer->GetMapId();
        state.zoneId = killer->GetZoneId();
        state.targetGuid = targetGuidLow;
        state.lastPromotionAt = now;
        state.lastVictimGuid = targetGuidLow;
        if (!existed)
            state.createdAt = now;
        state.lastSeenAt = now;
        RollAffixes(state);

        // Pass override key so rare nemeses keep writing to the original row.
        SaveNemesisState(killer, state, isRare ? saveSpawnId : 0);
        ApplyNemesisState(killer, state);
        killer->SetFullHealth();
        BroadcastRankFiveNemesisIfPersistent(killer, state);

        // Push nemesis data to the victim (the player who was killed)
        if (saveSpawnId && killed)
        {
            NemesisAddonView const view = BuildAddonView(killed, saveSpawnId, state);
            SendChunkedAddonPayload(killed, BuildAddonEntryPayload("UPSERT_VALIDATED", view));
        }

        if (ShouldAnnounceCreate() && state.rank >= GetAnnounceMinRank())
        {
            bool const reachedRankFive = existed && previousRank < 5 && state.rank >= 5;
            // [Немезида]: Бесопожар Обжористый достиг ранга 3!
            // [Немезида]: Бесопожар Обжористый стал(а) немезидой, убив Caraco!
            std::string message = existed
                ? Acore::StringFormat("[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: {} \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD0\xB8\xD0\xB3 \xD1\x80\xD0\xB0\xD0\xBD\xD0\xB3\xD0\xB0 {}!", killer->GetName(), state.rank)
                : Acore::StringFormat("[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: {} \xD1\x81\xD1\x82\xD0\xB0\xD0\xBB(\xD0\xB0) \xD0\xBD\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xBE\xD0\xB9, \xD1\x83\xD0\xB1\xD0\xB8\xD0\xB2 {}!", killer->GetName(), killed->GetName());
            BroadcastNemesisMessage(killer, message, reachedRankFive);
        }
    }
}

// Forward declaration for the bounty board completion check (full impl lives
// below, alongside the rest of the bounty board logic).
namespace NemesisBountyBoard
{
    bool CheckCompletion(Player* player, Creature* killed, std::string& outTitle, uint8& outRank);
}

class NemesisSystemPlayerScript : public PlayerScript
{
public:
    NemesisSystemPlayerScript() : PlayerScript("NemesisSystemPlayerScript", { PLAYERHOOK_ON_PLAYER_KILLED_BY_CREATURE, PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_CREATURE_KILLED_BY_PET, PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerLogin(Player* player) override
    {
        NemesisReputation::OnLogin(player);

        // Strip orphan familiar auras left over from a previous session.
        //
        // Why this is needed: familiar owner-auras are permanent + flagged
        // NO_AURA_CANCEL, so they survive player save. On logout the pet
        // despawns and `OnCreatureRemoveWorld` removes the aura in-memory,
        // but `Player::SaveToDB()` runs earlier in the logout sequence and
        // persists the still-active aura into `character_aura`. On next
        // login `Player::LoadFromDB()` re-applies it — but companions do
        // NOT auto-summon on login, so the aura ends up active with no
        // pet to back it.
        //
        // Stripping all familiar auras on login is safe: if the player
        // re-summons their pet, `OnCreatureAddWorld` re-applies both buff
        // and debuff via the standard path.
        for (uint32 spellId = 103000; spellId <= 103099; ++spellId)
            player->RemoveAurasDueToSpell(spellId);
        for (uint32 spellId = 104000; spellId <= 104099; ++spellId)
            player->RemoveAurasDueToSpell(spellId);
        // T1 fallback range (190010-012 pets) — drop when T1 migration lands.
        player->RemoveAurasDueToSpell(101100);
        player->RemoveAurasDueToSpell(101101);
        player->RemoveAurasDueToSpell(101102);
    }

    void OnPlayerLogout(Player* player) override
    {
        NemesisReputation::OnLogout(player);
        // Drop cached bounty pools for this player — they're cheap to
        // regenerate on next login and otherwise grow unboundedly across
        // logins over long server uptime.
        NemesisBountyBoard::InvalidatePool(player);
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (!IsEligibleNemesisKill(killer, killed))
            return;

        PromoteNemesis(killer, killed);
    }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (!killer || !killed)
            return;

        NemesisState state;
        if (!TryGetNemesisState(killed, state))
            return;

        bool const revenge = IsRevengeKill(killer, state);
        RewardRecipients const recipients = CollectRewardRecipients(killer, killed);
        float const rewardMultiplier = GetRewardMultiplier(killed->GetLevel(), recipients.highestLevel);

        for (Player* recipient : recipients.players)
            GrantReward(recipient, revenge, state.rank, rewardMultiplier, killed->GetLevel());

        // Hunter's Covenant reputation — gray-gated like token rewards.
        // Base + (nemesisRank × perRank) per eligible recipient.
        {
            bool const isGray = killed->GetLevel()
                <= Acore::XP::GetGrayLevel(recipients.highestLevel);
            if (!isGray)
            {
                uint32 const base = sConfigMgr->GetOption<uint32>("NemesisRep.BasePerKill", 50);
                uint32 const perRank = sConfigMgr->GetOption<uint32>("NemesisRep.PerRankBonus", 10);
                uint32 const award = base + perRank * uint32(state.rank);
                for (Player* recipient : recipients.players)
                    NemesisReputation::AddPoints(recipient, award);
            }
        }

        // Bounty contract completion — independent of gray-level gating.
        // Each recipient checks their own active bounty against this kill.
        for (Player* recipient : recipients.players)
        {
            std::string bountyTitle;
            uint8 bountyRank = 1;
            if (NemesisBountyBoard::CheckCompletion(recipient, killed, bountyTitle, bountyRank))
            {
                ChatHandler(recipient->GetSession()).PSendSysMessage(
                    "[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: "
                    "\xD0\x9A\xD0\xBE\xD0\xBD\xD1\x82\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82 \xD0\xB2\xD1\x8B\xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBD\xD0\xB5\xD0\xBD: {}",  // "Контракт выполнен: {}"
                    bountyTitle);

                // Bounty completion bonus rep — flat + per bounty rank.
                // Ungated: the board only offers level-appropriate targets,
                // so there's no exploit window for farming gray contracts.
                uint32 const bonusFlat = sConfigMgr->GetOption<uint32>(
                    "NemesisRep.BountyCompletionBonus", 200);
                uint32 const bonusPerRank = sConfigMgr->GetOption<uint32>(
                    "NemesisRep.BountyCompletionPerRank", 50);
                NemesisReputation::AddPoints(recipient,
                    bonusFlat + bonusPerRank * uint32(bountyRank));
            }
        }

        if (ShouldAnnounceKill() && state.rank >= GetAnnounceMinRank())
        {
            // [Немезида]: Honktu отомстил(а) Поганоглаз Ненасытный (ранг 1)!
            // [Немезида]: Fehkadrit устранил(а) Бесошлён Прожорливый (ранг 1)!
            std::string message = revenge
                ? Acore::StringFormat("[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: {} \xD0\xBE\xD1\x82\xD0\xBC\xD1\x81\xD1\x82\xD0\xB8\xD0\xBB(\xD0\xB0) {} (\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB3 {})!", killer->GetName(), killed->GetName(), state.rank)
                : Acore::StringFormat("[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: {} \xD1\x83\xD1\x81\xD1\x82\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xBB(\xD0\xB0) {} (\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB3 {})!", killer->GetName(), killed->GetName(), state.rank);
            BroadcastNemesisMessage(killed, message);
        }
    }

    void OnPlayerCreatureKilledByPet(Player* owner, Creature* killed) override
    {
        OnPlayerCreatureKill(owner, killed);
    }
};

// Familiar entry → owner-aura spell mapping.
// Gacha pool (10 families × 10 pets = 100 entries) lives at 191000-191099.
// Linear formula resolves each pet to:
//   buff aura   = 103000 + (entry - 191000)   (positive-only effects)
//   debuff aura = 104000 + (entry - 191000)   (negative-only effects)
// Both auras are applied on summon, removed on dismiss. The split keeps the
// buff on the buff bar and the debuff on the debuff bar — a mixed row would
// render as a single buff-bar icon with both lines in the tooltip, because
// client 3.3.5a classifies the spell as positive/negative as a whole.
// Auras are kept out of the summon spell itself because Effect_2 APPLY_AURA
// breaks companion-menu classification (implementation log bug #8).
// Cast site guards 104xxx with sSpellMgr lookup so pets without a debuff
// row (clean Commons, Rares) generate no log spam.
// T1 pets (190010-012) retained as fallback while Phase 3 migration is in
// flight; once T1 creature_template rows are dropped they become dead code.
struct FamiliarAuras
{
    uint32 buff = 0;
    uint32 debuff = 0;
};

static FamiliarAuras GetFamiliarOwnerAuraSpells(uint32 entry)
{
    if (entry >= 191000 && entry <= 191099)
    {
        uint32 const offset = entry - 191000;
        FamiliarAuras out;
        out.buff   = 103000 + offset;
        out.debuff = 104000 + offset; // optional; cast site checks sSpellMgr
        return out;
    }

    FamiliarAuras t1;
    switch (entry)
    {
        case 190010: t1.buff = 101100; return t1; // Guardian Wolf Cub → +1% armor
        case 190011: t1.buff = 101101; return t1; // Falcon Chick → +1% melee crit
        case 190012: t1.buff = 101102; return t1; // Raven Fledgling → +1% spell crit
    }
    return {};
}

// Resolves the player to apply/remove aura on. Minipets use TempSummon +
// summoner GUID; GetOwner() is usually null for SPELL_EFFECT_SUMMON with
// MINIPET properties. Resolve via the summoner guid and fall back to
// GetOwner for safety.
static Player* GetFamiliarOwnerPlayer(Creature* creature)
{
    if (!creature)
        return nullptr;
    if (TempSummon* temp = creature->ToTempSummon())
        if (Unit* s = temp->GetSummonerUnit())
            if (Player* player = s->ToPlayer())
                return player;
    if (Unit* owner = creature->GetOwner())
        return owner->ToPlayer();
    return nullptr;
}

class NemesisSystemAllCreatureScript : public AllCreatureScript
{
public:
    NemesisSystemAllCreatureScript() : AllCreatureScript("NemesisSystemAllCreatureScript") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        FamiliarAuras const auras = GetFamiliarOwnerAuraSpells(creature->GetEntry());
        if (auras.buff)
        {
            if (Player* player = GetFamiliarOwnerPlayer(creature))
            {
                player->CastSpell(player, auras.buff, true);
                if (auras.debuff && sSpellMgr->GetSpellInfo(auras.debuff))
                    player->CastSpell(player, auras.debuff, true);
            }
            return;
        }

        NemesisState state;
        bool found = TryGetNemesisState(creature, state);

        // Rare-mob pool rotation: the spawning spawnId doesn't match any
        // nemesis row directly, but another spawn point of this rare is
        // already a nemesis. Apply that state to the new spawn and update
        // all matching rare nemesis rows to the current position.
        bool rarePickup = false;
        if (!found && IsRareEntry(creature->GetEntry()) && creature->GetSpawnId())
        {
            if (ObjectGuid::LowType const existingKey = FindAnyRareNemesisSpawnId(creature->GetEntry()))
            {
                if (TryGetNemesisState(existingKey, state))
                {
                    found = true;
                    rarePickup = true;
                }
            }
        }

        if (!found)
            return;

        ApplyNemesisState(creature, state);

        uint32 const now = uint32(GameTime::GetGameTime().count());
        float const posX = creature->GetPositionX();
        float const posY = creature->GetPositionY();
        float const posZ = creature->GetPositionZ();
        uint32 const zoneId = creature->GetZoneId();

        state.zoneId = zoneId;
        state.homeX = posX;
        state.homeY = posY;
        state.homeZ = posZ;
        state.lastSeenAt = now;

        if (rarePickup)
        {
            // Update every rare nemesis row for this entry so all players
            // affected by the rare see its new position.
            for (auto& [key, s] : ActiveNemeses)
            {
                if (s.creatureEntry == creature->GetEntry())
                {
                    s.zoneId = zoneId;
                    s.homeX = posX;
                    s.homeY = posY;
                    s.homeZ = posZ;
                    s.lastSeenAt = now;
                }
            }
        }
        else if (ObjectGuid::LowType const spawnId = creature->GetSpawnId())
            ActiveNemeses[spawnId] = state;
        else if (IsTemporaryNemesisCandidate(creature))
            ActiveTemporaryNemeses[creature->GetGUID()] = state;
    }

    void OnCreatureRemoveWorld(Creature* creature) override
    {
        if (!creature)
            return;

        FamiliarAuras const auras = GetFamiliarOwnerAuraSpells(creature->GetEntry());
        if (auras.buff)
        {
            if (Player* player = GetFamiliarOwnerPlayer(creature))
            {
                player->RemoveAurasDueToSpell(auras.buff);
                if (auras.debuff)
                    player->RemoveAurasDueToSpell(auras.debuff);
            }
            return;
        }

        if (creature->GetSpawnId())
            return;

        ActiveTemporaryNemeses.erase(creature->GetGUID());
        TemporaryRegenTickAccumulators.erase(creature->GetGUID());
    }

    void OnAllCreatureUpdate(Creature* creature, uint32 /*diff*/) override
    {
        if (!creature)
            return;

        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        if (creature->IsAlive())
            return;

        EraseRegenAccumulator(creature);
        DeleteNemesisState(creature, "dead");
    }
};

class NemesisSystemUnitScript : public UnitScript
{
public:
    NemesisSystemUnitScript() : UnitScript("NemesisSystemUnitScript", true, { UNITHOOK_ON_DAMAGE, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN, UNITHOOK_ON_UNIT_UPDATE }) { }

    void OnDamage(Unit* attacker, Unit* /*victim*/, uint32& damage) override
    {
        if (!attacker || !damage || !attacker->IsCreature())
            return;

        Creature* creature = attacker->ToCreature();

        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        if (HasAffix(state, NEMESIS_AFFIX_SAVAGE))
            damage = uint32(float(damage) * GetSavageDamageMultiplier());

        if (HasAffix(state, NEMESIS_AFFIX_ENRAGED) && IsBelowEnrageThreshold(creature))
            damage = uint32(float(damage) * GetEnragedDamageMultiplier());

        if (!HasAffix(state, NEMESIS_AFFIX_VAMPIRIC))
            return;

        uint32 healAmount = std::max<uint32>(1, uint32(float(damage) * GetVampiricHealPct()));
        creature->ModifyHealth(int32(healAmount));
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (!target || !attacker || damage <= 0 || !target->IsCreature())
            return;

        Creature* creature = target->ToCreature();

        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        if (!HasAffix(state, NEMESIS_AFFIX_SPELLWARD))
            return;

        damage = std::max<int32>(1, int32(float(damage) * GetSpellwardDamageMultiplier()));
    }

    void OnUnitUpdate(Unit* unit, uint32 diff) override
    {
        if (!unit || !unit->IsCreature())
            return;

        Creature* creature = unit->ToCreature();

        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        if (!HasAffix(state, NEMESIS_AFFIX_REGEN) || !creature->IsAlive() || creature->GetHealth() >= creature->GetMaxHealth())
        {
            EraseRegenAccumulator(creature);
            return;
        }

        uint32& accumulator = GetRegenAccumulator(creature);
        accumulator += diff;

        uint32 const interval = GetRegenerationIntervalMs();
        if (accumulator < interval)
            return;

        accumulator %= interval;

        uint32 healAmount = std::max<uint32>(1, uint32(float(creature->GetMaxHealth()) * (GetRegenerationHealthPct() / 100.0f)));
        creature->ModifyHealth(int32(healAmount));
    }
};

class NemesisSystemCommandScript : public CommandScript
{
public:
    NemesisSystemCommandScript() : CommandScript("NemesisSystemCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable addonTable =
        {
            { "bootstrap", HandleAddonBootstrap, SEC_PLAYER, Console::No },
            { "report", HandleAddonReport, SEC_PLAYER, Console::No },
            { "history", HandleAddonHistory, SEC_PLAYER, Console::No },
            { "sync", HandleAddonSync, SEC_GAMEMASTER, Console::No }
        };

        static ChatCommandTable nemesisTable =
        {
            { "addon", addonTable },
            { "debug", HandleDebug, SEC_GAMEMASTER, Console::No },
            { "info", HandleInfo, SEC_GAMEMASTER, Console::No },
            { "mark", HandleMark, SEC_GAMEMASTER, Console::No },
            { "reroll", HandleReroll, SEC_GAMEMASTER, Console::No },
            { "list", HandleList, SEC_GAMEMASTER, Console::No },
            { "clear", HandleClear, SEC_GAMEMASTER, Console::No },
            { "mapclear", HandleMapClear, SEC_GAMEMASTER, Console::No },
            { "clearall", HandleClearAll, SEC_ADMINISTRATOR, Console::Yes },
            { "reload", HandleReload, SEC_ADMINISTRATOR, Console::Yes },
            { "merge-rares", HandleMergeRares, SEC_ADMINISTRATOR, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "nemesis", nemesisTable }
        };

        return commandTable;
    }

    static bool HandleAddonBootstrap(ChatHandler* handler)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
        {
            handler->PSendSysMessage("You must be logged in as a player to request addon bootstrap data.");
            return true;
        }

        SendNemesisBootstrap(player, true);
        return true;
    }

    static bool HandleAddonReport(ChatHandler* handler, uint64 rawSpawnId, Optional<uint32> entryArg)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
        {
            handler->PSendSysMessage("You must be logged in as a player to report addon sightings.");
            return true;
        }

        ObjectGuid::LowType const spawnId = ObjectGuid::LowType(rawSpawnId);
        NemesisState state;
        if (!TryGetNemesisState(spawnId, state))
            return true;

        // Verify the player's selected target actually IS the nemesis being
        // reported. Without this check, a buggy or malicious client could
        // overwrite the row with the player's location for any spawn_id
        // (the original cause of corrupted Thunder Bluff coordinates on
        // Stranglethorn-resident nemeses — see addon parseCreatureGuid).
        Unit* targetUnit = ObjectAccessor::GetUnit(*player, player->GetTarget());
        Creature* creature = targetUnit ? targetUnit->ToCreature() : nullptr;
        if (!creature || creature->GetSpawnId() != spawnId)
            return true;

        if (entryArg && *entryArg != 0 && creature->GetEntry() != *entryArg)
            return true;

        uint32 const now = uint32(GameTime::GetGameTime().count());
        if (state.lastSeenAt && state.lastSeenAt + GetAddonReportCooldownSeconds() > now)
            return true;

        state.mapId = creature->GetMapId();
        state.zoneId = creature->GetZoneId();
        state.homeX = creature->GetPositionX();
        state.homeY = creature->GetPositionY();
        state.homeZ = creature->GetPositionZ();
        state.lastSeenAt = now;

        ActiveNemeses[spawnId] = state;
        CharacterDatabase.Execute(
            "UPDATE `character_nemesis` SET `map_id` = {}, `zone_id` = {}, `pos_x` = {}, `pos_y` = {}, `pos_z` = {}, `last_seen_at` = {} WHERE `guid` = {}",
            state.mapId,
            state.zoneId,
            state.homeX,
            state.homeY,
            state.homeZ,
            state.lastSeenAt,
            uint64(spawnId));

        SendValidatedNemesisUpsert(player, spawnId, state);
        return true;
    }

    static bool HandleAddonSync(ChatHandler* handler)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
        {
            handler->PSendSysMessage("You must be logged in as a player to sync addon data.");
            return true;
        }

        SendNemesisBootstrap(player, true);
        return true;
    }

    static bool HandleAddonHistory(ChatHandler* handler)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
            return true;

        SendNemesisBountyHistory(player);
        return true;
    }

    static bool HandleDebug(ChatHandler* handler)
    {
        Creature* target = handler->getSelectedCreature();
        if (!target)
        {
            handler->PSendSysMessage("You must select a creature.");
            return true;
        }

        handler->PSendSysMessage("Nemesis target: {} (entry {}, spawn {}, map {})", target->GetName(), target->GetEntry(), uint64(target->GetSpawnId()), target->GetMapId());

        NemesisState state;
        if (!TryGetNemesisState(target, state))
        {
            handler->PSendSysMessage("Selected creature is not an active nemesis.");
            return true;
        }

        handler->PSendSysMessage("Rank {} | Affixes {} | TargetGuid {}", state.rank, GetAffixList(state.affixMask), state.targetGuid);
        handler->PSendSysMessage("Health {} / {} | Scale {}", target->GetHealth(), target->GetMaxHealth(), target->GetObjectScale());
        handler->PSendSysMessage("Main damage {} - {}", target->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE), target->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE));
        handler->PSendSysMessage("Rank-up cooldown remaining {}s | Same victim cooldown remaining {}s", GetRankUpCooldownRemaining(state), GetSameVictimCooldownRemaining(state, state.targetGuid));
        return true;
    }

    static bool HandleInfo(ChatHandler* handler, uint64 rawSpawnId)
    {
        ObjectGuid::LowType const spawnId = ObjectGuid::LowType(rawSpawnId);

        NemesisState state;
        if (!TryGetNemesisState(spawnId, state))
        {
            handler->PSendSysMessage("Spawn {} is not an active nemesis.", rawSpawnId);
            return true;
        }

        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        Map* map = player ? player->GetMap() : nullptr;
        if (map && map->GetId() != state.mapId)
            map = nullptr;

        Creature* liveCreature = FindLoadedCreatureBySpawnId(map, spawnId);
        std::string name = GetNemesisDisplayName(map, spawnId, state);

        handler->PSendSysMessage("Spawn {} | {} | Entry {} | Map {}", rawSpawnId, name, state.creatureEntry, state.mapId);
        handler->PSendSysMessage("Rank {} | Affixes {} | Target {}", state.rank, GetAffixList(state.affixMask), state.targetGuid);
        handler->PSendSysMessage("Rank-up cooldown remaining {}s | Same victim cooldown remaining {}s", GetRankUpCooldownRemaining(state), GetSameVictimCooldownRemaining(state, state.targetGuid));
        if (liveCreature)
            handler->PSendSysMessage("Loaded now | HP {}/{} | Scale {}", liveCreature->GetHealth(), liveCreature->GetMaxHealth(), liveCreature->GetObjectScale());
        else
            handler->PSendSysMessage("Not currently loaded on your map.");

        return true;
    }

    static bool HandleMark(ChatHandler* handler, Optional<uint8> rankArg)
    {
        Creature* target = handler->getSelectedCreature();
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!target || !player)
        {
            handler->PSendSysMessage("You must select a creature while logged in as a player.");
            return true;
        }

        NemesisState state;
        if (!TryGetNemesisState(target, state))
            state = BuildInitialNemesisState(target, player);

        uint8 rank = state.rank;
        if (rankArg)
            rank = std::clamp<uint8>(*rankArg, 1, GetMaxRank());
        else if (rank < GetMaxRank())
            ++rank;

        state.rank = rank;
        state.targetGuid = player->GetGUID().GetCounter();
        RollAffixes(state);
        state.lastSeenAt = uint32(GameTime::GetGameTime().count());
        SaveNemesisState(target, state);
        ApplyNemesisState(target, state);
        target->SetFullHealth();
        BroadcastRankFiveNemesisIfPersistent(target, state);
        handler->PSendSysMessage("Marked {} as nemesis rank {} with affixes {}.", target->GetName(), state.rank, GetAffixList(state.affixMask));
        return true;
    }

    static bool HandleClear(ChatHandler* handler)
    {
        Creature* target = handler->getSelectedCreature();
        if (!target)
        {
            handler->PSendSysMessage("You must select a creature.");
            return true;
        }

        NemesisState state;
        if (!TryGetNemesisState(target, state))
        {
            handler->PSendSysMessage("Selected creature is not an active nemesis.");
            return true;
        }

        ResetCreatureToBaseState(target, state);
        DeleteNemesisState(target);
        handler->PSendSysMessage("Cleared nemesis state from {}.", target->GetName());
        return true;
    }

    static bool HandleReroll(ChatHandler* handler)
    {
        Creature* target = handler->getSelectedCreature();
        if (!target)
        {
            handler->PSendSysMessage("You must select a creature.");
            return true;
        }

        NemesisState state;
        if (!TryGetNemesisState(target, state))
        {
            handler->PSendSysMessage("Selected creature is not an active nemesis.");
            return true;
        }

        state.affixMask = 0;
        RollAffixes(state);
        state.lastSeenAt = uint32(GameTime::GetGameTime().count());
        SaveNemesisState(target, state);
        ApplyNemesisState(target, state);
        handler->PSendSysMessage("Rerolled affixes for {}: {}.", target->GetName(), GetAffixList(state.affixMask));
        return true;
    }

    static bool HandleList(ChatHandler* handler)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
        {
            handler->PSendSysMessage("You must be logged in as a player to list map nemeses.");
            return true;
        }

        Map* map = player->GetMap();
        if (!map)
        {
            handler->PSendSysMessage("Unable to resolve your current map.");
            return true;
        }

        uint32 count = 0;
        handler->PSendSysMessage("Active nemeses on map {}:", map->GetId());

        for (auto const& [spawnId, state] : ActiveNemeses)
        {
            if (state.mapId != map->GetId())
                continue;

            Creature* liveCreature = FindLoadedCreatureBySpawnId(map, spawnId);
            std::string name = GetNemesisDisplayName(map, spawnId, state);

            handler->PSendSysMessage("Spawn {} | {} | Rank {} | Affixes {} | Target {}{}",
                uint64(spawnId),
                name,
                state.rank,
                GetAffixList(state.affixMask),
                state.targetGuid,
                liveCreature ? Acore::StringFormat(" | HP {}/{}", liveCreature->GetHealth(), liveCreature->GetMaxHealth()) : "");
            ++count;
        }

        for (auto const& [guid, state] : ActiveTemporaryNemeses)
        {
            if (state.mapId != map->GetId())
                continue;

            Creature* liveCreature = ObjectAccessor::GetCreature(*player, guid);
            if (!liveCreature)
                continue;

            handler->PSendSysMessage("Temporary {} | {} | Rank {} | Affixes {} | Target {} | HP {}/{}",
                guid.GetCounter(),
                liveCreature->GetName(),
                state.rank,
                GetAffixList(state.affixMask),
                state.targetGuid,
                liveCreature->GetHealth(),
                liveCreature->GetMaxHealth());
            ++count;
        }

        if (!count)
            handler->PSendSysMessage("No active nemeses found on this map.");
        else
            handler->PSendSysMessage("Total active nemeses on this map: {}.", count);

        return true;
    }

    static bool HandleMapClear(ChatHandler* handler)
    {
        Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
        if (!player)
        {
            handler->PSendSysMessage("You must be logged in as a player to clear map nemeses.");
            return true;
        }

        Map* map = player->GetMap();
        if (!map)
        {
            handler->PSendSysMessage("Unable to resolve your current map.");
            return true;
        }

        std::vector<ObjectGuid::LowType> spawnIds;
        spawnIds.reserve(ActiveNemeses.size());

        for (auto const& [spawnId, state] : ActiveNemeses)
            if (state.mapId == map->GetId())
                spawnIds.push_back(spawnId);

        if (spawnIds.empty())
        {
            handler->PSendSysMessage("No active nemeses found on this map.");
            return true;
        }

        for (ObjectGuid::LowType spawnId : spawnIds)
        {
            NemesisState state;
            if (!TryGetNemesisState(spawnId, state))
                continue;

            if (Creature* liveCreature = FindLoadedCreatureBySpawnId(map, spawnId))
                ResetCreatureToBaseState(liveCreature, state);

            DeleteNemesisState(spawnId);
        }

        std::vector<ObjectGuid> temporaryGuids;
        temporaryGuids.reserve(ActiveTemporaryNemeses.size());

        for (auto const& [guid, state] : ActiveTemporaryNemeses)
            if (state.mapId == map->GetId())
                temporaryGuids.push_back(guid);

        for (ObjectGuid const& guid : temporaryGuids)
            if (Creature* liveCreature = ObjectAccessor::GetCreature(*player, guid))
            {
                NemesisState state;
                if (!TryGetNemesisState(liveCreature, state))
                    continue;

                ResetCreatureToBaseState(liveCreature, state);
                DeleteNemesisState(liveCreature);
            }

        handler->PSendSysMessage("Cleared {} active nemesis record(s) from map {}.", spawnIds.size() + temporaryGuids.size(), map->GetId());
        return true;
    }

    static bool HandleClearAll(ChatHandler* handler)
    {
        EnsureCacheLoaded();

        // Materialize bounty targets before mutating — we can't run another
        // query while iterating the first result set on the same connection.
        std::vector<uint32> bountyTargets;
        if (QueryResult res = CharacterDatabase.Query(
            "SELECT DISTINCT `target_spawn_id` FROM `character_nemesis_bounty`"))
        {
            do
            {
                bountyTargets.push_back(res->Fetch()[0].Get<uint32>());
            } while (res->NextRow());
        }

        for (uint32 target : bountyTargets)
            NemesisBountyBoard::RefundBountiesForTarget(target);

        // DeleteNemesisState mutates ActiveNemeses, so snapshot the keys first.
        // Going through DeleteNemesisState (rather than wiping the map) so
        // BroadcastNemesisRemove fires for each and addons drop their pins.
        std::vector<ObjectGuid::LowType> spawnIds;
        spawnIds.reserve(ActiveNemeses.size());
        for (auto const& [id, _] : ActiveNemeses)
            spawnIds.push_back(id);

        for (ObjectGuid::LowType id : spawnIds)
            DeleteNemesisState(id, "cleared");

        ActiveTemporaryNemeses.clear();
        TemporaryRegenTickAccumulators.clear();

        // Safety net for any rows the per-spawn path didn't cover.
        CharacterDatabase.Execute("DELETE FROM `character_nemesis`");

        NemesisBountyBoard::InvalidateAllPools();

        handler->PSendSysMessage("Cleared all stored nemesis records.");
        return true;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        if (!sConfigMgr->LoadModulesConfigs(true, false))
        {
            handler->PSendSysMessage("Nemesis System configuration reload failed.");
            return true;
        }

        handler->PSendSysMessage("Nemesis System configuration reloaded.");
        return true;
    }

    // Backfill cleanup: collapses duplicate rare-mob nemesis rows per
    // (target_player, creature_entry). Keeps the row with the highest rank,
    // deletes the others. Needed once after upgrading to entry-keyed rare
    // nemeses so pre-existing pool-rotation duplicates disappear.
    static bool HandleMergeRares(ChatHandler* handler)
    {
        EnsureCacheLoaded();

        // Group by (targetGuid, creatureEntry) — only rare entries.
        struct Group
        {
            ObjectGuid::LowType keep = 0;
            uint8 keepRank = 0;
            std::vector<ObjectGuid::LowType> toDelete;
        };
        std::unordered_map<uint64, Group> groups;

        for (auto const& [spawnId, state] : ActiveNemeses)
        {
            if (!IsRareEntry(state.creatureEntry))
                continue;
            uint64 const key = (uint64(state.targetGuid) << 32) | uint64(state.creatureEntry);
            Group& g = groups[key];
            if (state.rank > g.keepRank)
            {
                if (g.keep)
                    g.toDelete.push_back(g.keep);
                g.keep = spawnId;
                g.keepRank = state.rank;
            }
            else
                g.toDelete.push_back(spawnId);
        }

        uint32 mergedGroups = 0;
        uint32 deletedRows = 0;
        for (auto const& [key, g] : groups)
        {
            if (g.toDelete.empty())
                continue;
            ++mergedGroups;
            for (ObjectGuid::LowType spawnId : g.toDelete)
            {
                CharacterDatabase.Execute(
                    "DELETE FROM `character_nemesis` WHERE `guid` = {}", uint64(spawnId));
                ActiveNemeses.erase(spawnId);
                ++deletedRows;
            }
        }

        handler->PSendSysMessage("Merged {} rare-mob nemesis group(s), deleted {} duplicate row(s).",
            mergedGroups, deletedRows);
        return true;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// Bounty Board — per-player daily rotating contracts on active nemeses
// ────────────────────────────────────────────────────────────────────────────
namespace NemesisBountyBoard
{
    struct Bounty
    {
        ObjectGuid::LowType spawnId = 0;
        uint32 creatureEntry = 0;
        uint32 mapId = 0;
        uint32 zoneId = 0;
        uint8 level = 0;
        uint8 rank = 1;
        std::string title;
    };

    struct DailyPool
    {
        uint32 generatedAt = 0;
        std::vector<Bounty> bounties;
    };

    // Virtual NPC entry used as the "From:" on bounty completion and refund
    // mails. Defined in nemesis_bounty_vendor.sql (creature_template row with
    // ASCII name "Innkeeper" — the Russian 3.3.5a client mojibakes UTF-8
    // creature names in the mailbox, so we keep it English).
    static constexpr uint32 MAIL_SENDER_ENTRY = 190002;

    // Pools are keyed by (playerGuidLow, innkeeperZoneId) — each zone gets its
    // own rotation for the day. Encoded as a uint64 with player low GUID in
    // the high 32 bits and zoneId in the low 32 bits.
    std::unordered_map<uint64, DailyPool> PlayerDailyPools;

    inline uint64 MakePoolKey(Player* player, uint32 zoneId)
    {
        return (uint64(player->GetGUID().GetCounter()) << 32) | uint64(zoneId);
    }

    bool IsEnabled() { return sConfigMgr->GetOption<bool>("NemesisSystem.BountyBoard.Enable", true); }
    uint32 GetSlotsPerDay() { return std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.SlotsPerDay", 3)); }
    uint32 GetDurationHours() { return std::max<uint32>(1u, sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.DurationHours", 24)); }
    uint32 GetLevelRangeDown() { return sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.LevelRangeDown", 10); }
    uint32 GetLevelRangeUp() { return sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.LevelRangeUp", 10); }

    // Unix timestamp of the most recent pool refresh tick. Pool rotates
    // every PoolRefreshHours (default 2h) — short enough that a cleared
    // zone refreshes reasonably soon, long enough that players don't just
    // re-roll the list by closing and reopening the innkeeper gossip.
    uint32 GetLastDailyResetTs()
    {
        uint32 const ttl = std::max<uint32>(1u,
            sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.PoolRefreshHours", 2)) * 3600u;
        uint32 const now = uint32(GameTime::GetGameTime().count());
        return (now / ttl) * ttl;  // aligned to TTL boundary
    }

    // Zone gating is strict: the bounty pool is filtered to nemeses in the
    // innkeeper's *zone*. City inns (Stormwind, Ironforge, etc.) are safe
    // zones with no nemeses, so they will show an empty board — that's
    // intentional, the innkeeper only knows about troubles in their own area.
    // No IP-mod coupling needed (zone gating is stricter than continent).

    // Resolve the creature level for a nemesis — live creature if loaded,
    // otherwise creature template's maxlevel as fallback.
    uint8 GetNemesisLevel(ObjectGuid::LowType spawnId, NemesisState const& state)
    {
        if (Map* map = sMapMgr->FindBaseNonInstanceMap(state.mapId))
            if (Creature* live = FindLoadedCreatureBySpawnId(map, spawnId))
                return live->GetLevel();

        if (CreatureTemplate const* ct = sObjectMgr->GetCreatureTemplate(state.creatureEntry))
            return uint8(ct->maxlevel);

        return 0;
    }

    // Picks up to SlotsPerDay bounties from ActiveNemeses, filtered by:
    //  - nemesis in the innkeeper's zone (local intel — the innkeeper only
    //    knows about troubles in their own area; city inns will be empty)
    //  - not currently the player's target (no self-bounty)
    //  - optional level window (0 = unlimited — default, since the bounty
    //    contract reward is independent of the gray-level-gated regular kill
    //    reward). Admins can set LevelRangeDown/Up > 0 to reimpose a window.
    // Sorts by (rank DESC, level DESC) and fills slots tier by tier.
    // Temporary nemeses excluded (their GUIDs change on restart).
    DailyPool GenerateDailyPool(Player* player, uint32 innkeeperZoneId)
    {
        DailyPool pool;
        pool.generatedAt = uint32(GameTime::GetGameTime().count());

        // Make sure ActiveNemeses is populated — this is idempotent after first call.
        EnsureCacheLoaded();

        uint8 const pLvl = player->GetLevel();
        int32 const rangeDown = int32(GetLevelRangeDown());
        int32 const rangeUp = int32(GetLevelRangeUp());
        uint32 const playerGuid = player->GetGUID().GetCounter();

        std::vector<Bounty> eligible;
        eligible.reserve(ActiveNemeses.size());

        for (auto const& [spawnId, state] : ActiveNemeses)
        {
            // Only nemeses in the innkeeper's zone (local intel).
            if (state.zoneId != innkeeperZoneId)
                continue;

            // Don't offer a bounty on a nemesis that was created specifically
            // for this player — no self-revenge contracts.
            if (state.targetGuid == playerGuid)
                continue;

            uint8 nLvl = GetNemesisLevel(spawnId, state);
            if (!nLvl)
                continue;

            if (rangeDown > 0 && int32(nLvl) < int32(pLvl) - rangeDown) continue;
            if (rangeUp > 0 && int32(nLvl) > int32(pLvl) + rangeUp)     continue;

            Bounty b;
            b.spawnId = spawnId;
            b.creatureEntry = state.creatureEntry;
            b.mapId = state.mapId;
            b.zoneId = state.zoneId;
            b.level = nLvl;
            b.rank = state.rank;
            b.title = GenerateNemesisTitle(spawnId, state.creatureEntry);
            eligible.push_back(std::move(b));
        }

        if (eligible.empty())
            return pool;

        // Sort by (rank DESC, level DESC) — rank dominates because a rank-6
        // nemesis is more interesting than a rank-1 one at a higher level.
        std::sort(eligible.begin(), eligible.end(),
            [](Bounty const& a, Bounty const& b)
            {
                if (a.rank != b.rank) return a.rank > b.rank;
                return a.level > b.level;
            });

        // Deterministic shuffle seed so two innkeepers in the same zone show
        // the same list (seed mixes player, zone, and daily reset timestamp).
        uint32 const seed = playerGuid ^ innkeeperZoneId ^ GetLastDailyResetTs();
        std::mt19937 rng(seed);

        uint32 const slots = GetSlotsPerDay();

        // Fill slots tier by tier (rank+level), shuffling within each tier for
        // variety across daily resets when more candidates than slots exist.
        size_t idx = 0;
        while (pool.bounties.size() < slots && idx < eligible.size())
        {
            uint8 tierRank = eligible[idx].rank;
            uint8 tierLevel = eligible[idx].level;
            size_t tierStart = idx;
            while (idx < eligible.size()
                && eligible[idx].rank == tierRank
                && eligible[idx].level == tierLevel)
                ++idx;
            size_t tierSize = idx - tierStart;

            std::vector<size_t> tierIndices(tierSize);
            std::iota(tierIndices.begin(), tierIndices.end(), tierStart);
            std::shuffle(tierIndices.begin(), tierIndices.end(), rng);

            for (size_t ti : tierIndices)
            {
                if (pool.bounties.size() >= slots)
                    break;
                pool.bounties.push_back(eligible[ti]);
            }
        }

        return pool;
    }

    // Always regenerates from live ActiveNemeses. Deterministic shuffle
    // (seed = playerGuid ^ zoneId ^ 2h-aligned-window) keeps the order stable
    // within the 2h window, but newly-promoted nemeses now appear the moment
    // you open the board — no waiting for a TTL rollover. The completion
    // filter in ShowBoard uses GetLastDailyResetTs() directly, so completed
    // bounties still stay hidden for the full 2h window.
    DailyPool& GetOrGeneratePool(Player* player, uint32 innkeeperZoneId)
    {
        uint64 const key = MakePoolKey(player, innkeeperZoneId);
        PlayerDailyPools[key] = GenerateDailyPool(player, innkeeperZoneId);
        return PlayerDailyPools[key];
    }

    void InvalidatePool(Player* player)
    {
        if (!player) return;
        uint32 const guidLow = player->GetGUID().GetCounter();
        // Drop all per-map pools for this player.
        for (auto it = PlayerDailyPools.begin(); it != PlayerDailyPools.end(); )
            if (uint32(it->first >> 32) == guidLow)
                it = PlayerDailyPools.erase(it);
            else
                ++it;
    }

    // Nuke every cached pool — used after bulk state mutations like
    // .nemesis clearall so the next innkeeper open regenerates from the
    // (just-changed) ActiveNemeses map instead of serving stale snapshots.
    void InvalidateAllPools()
    {
        PlayerDailyPools.clear();
    }

    // ─────────────────────────────────────────────────────────────────────
    // Active bounty lifecycle (accept / check / abandon / complete)
    // Struct ActiveBounty is forward-declared at the top of this file.
    // ─────────────────────────────────────────────────────────────────────

    uint32 GetRewardTokens(uint8 rank)
    {
        uint32 const base  = sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.RewardTokens", 3);
        uint32 const perRk = sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.RewardTokensPerRank", 2);
        return base + perRk * uint32(rank);
    }

    uint32 GetRewardGold(uint8 rank)
    {
        uint32 const base  = sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.RewardGold", 5000);
        uint32 const perRk = sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.RewardGoldPerRank", 5000);
        return base + perRk * uint32(rank);
    }

    uint32 GetRewardTokenItemId()
    {
        // Defaults to 100017 (Nemesis Bounty Token) — same item the regular
        // reward system uses. Admins can override via the BountyRewardItem
        // option since both flows share the same token currency.
        return sConfigMgr->GetOption<uint32>("NemesisSystem.BountyRewardItem", 100017);
    }

    // Returns true if the player has an active, non-expired bounty.
    // Expired rows are pruned on access. Out params populated on success.
    bool GetActiveBounty(Player* player, ActiveBounty& out)
    {
        if (!player) return false;

        uint32 const guidLow = player->GetGUID().GetCounter();
        QueryResult result = CharacterDatabase.Query(
            "SELECT `target_spawn_id`, `target_title`, `zone_id`, `accepted_at`, `expires_at` "
            "FROM `character_nemesis_bounty` WHERE `guid` = {}", guidLow);
        if (!result)
            return false;

        Field* f = result->Fetch();
        out.targetSpawnId = f[0].Get<uint32>();
        out.targetTitle   = f[1].Get<std::string>();
        out.zoneId        = f[2].Get<uint32>();
        out.acceptedAt    = f[3].Get<uint32>();
        out.expiresAt     = f[4].Get<uint32>();

        uint32 const now = uint32(GameTime::GetGameTime().count());
        if (out.expiresAt <= now)
        {
            CharacterDatabase.Execute(
                "DELETE FROM `character_nemesis_bounty` WHERE `guid` = {}", guidLow);
            // Notify once; subsequent calls find no row and return early.
            // Client addon parses this to clear its bounty highlight.
            ChatHandler(player->GetSession()).PSendSysMessage(
                // "[Немезида]: Контракт на {} истёк."
                "[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: "
                "\xD0\x9A\xD0\xBE\xD0\xBD\xD1\x82\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82 "
                "\xD0\xBD\xD0\xB0 {} "
                "\xD0\xB8\xD1\x81\xD1\x82\xD1\x91\xD0\xBA.",
                out.targetTitle);
            return false;
        }

        return true;
    }

    // Server-wide announce helpers. Gated by config flags so operators can
    // turn them off on crowded realms where the chat spam would be annoying.
    // Red color (|cffff0000) plus the [Немезида] prefix the core module
    // already uses for its other announcements.
    void AnnounceAccept(Player* player, std::string const& title)
    {
        if (!player) return;
        if (!sConfigMgr->GetOption<bool>("NemesisSystem.BountyBoard.AnnounceAccept", true))
            return;
        // "|cffff0000[Немезида]: {} принял(а) контракт на {}!|r"
        std::string msg = Acore::StringFormat(
            "|cffff0000[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: {} "
            "\xD0\xBF\xD1\x80\xD0\xB8\xD0\xBD\xD1\x8F\xD0\xBB(\xD0\xB0) "
            "\xD0\xBA\xD0\xBE\xD0\xBD\xD1\x82\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82 \xD0\xBD\xD0\xB0 {}!|r",
            player->GetName(), title);
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, msg);
    }

    void AnnounceCompletion(Player* player, std::string const& title)
    {
        if (!player) return;
        if (!sConfigMgr->GetOption<bool>("NemesisSystem.BountyBoard.AnnounceCompletion", true))
            return;
        // "|cffff0000[Немезида]: {} выполнил(а) контракт на {}!|r"
        std::string msg = Acore::StringFormat(
            "|cffff0000[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: {} "
            "\xD0\xB2\xD1\x8B\xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBD\xD0\xB8\xD0\xBB(\xD0\xB0) "
            "\xD0\xBA\xD0\xBE\xD0\xBD\xD1\x82\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82 \xD0\xBD\xD0\xB0 {}!|r",
            player->GetName(), title);
        sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, msg);
    }

    // Accepts a bounty contract — writes the row (REPLACE replaces any
    // leftover row, but the UI flow already prompts abandon before accept).
    // zoneId is the innkeeper's zone — used for the per-zone completion cap.
    void AcceptBounty(Player* player, Bounty const& b, uint32 zoneId)
    {
        if (!player) return;

        uint32 const guidLow = player->GetGUID().GetCounter();
        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const hours = sConfigMgr->GetOption<uint32>("NemesisSystem.BountyBoard.DurationHours", 24);
        uint32 const expires = now + hours * 3600u;

        std::string title = b.title;
        CharacterDatabase.EscapeString(title);

        // DirectExecute — synchronous so the subsequent gossip refresh sees
        // the new row immediately (async Execute would cause a stale read).
        CharacterDatabase.DirectExecute(
            "REPLACE INTO `character_nemesis_bounty` "
            "(`guid`, `target_spawn_id`, `target_title`, `zone_id`, `accepted_at`, `expires_at`) "
            "VALUES ({}, {}, '{}', {}, {}, {})",
            guidLow, uint32(b.spawnId), title, zoneId, now, expires);

        AnnounceAccept(player, b.title);
    }

    void AbandonBounty(Player* player)
    {
        if (!player) return;
        CharacterDatabase.DirectExecute(
            "DELETE FROM `character_nemesis_bounty` WHERE `guid` = {}",
            player->GetGUID().GetCounter());
    }

    // Called from DeleteNemesisState — when a nemesis is cleared by any
    // means (admin command, expiry cleanup, kill-and-remove, merge-rares),
    // refund any players holding a contract on that target. Works for
    // offline players (mail queued via GUID). If RefundTokensOnAdminClear
    // is 0, rows are deleted silently with no mail.
    void RefundBountiesForTarget(ObjectGuid::LowType targetSpawnId)
    {
        if (!targetSpawnId)
            return;

        QueryResult res = CharacterDatabase.Query(
            "SELECT `guid`, `target_title` FROM `character_nemesis_bounty` "
            "WHERE `target_spawn_id` = {}", uint32(targetSpawnId));
        if (!res)
            return;

        struct Affected { uint32 guid; std::string title; };
        std::vector<Affected> affected;
        do
        {
            Field* f = res->Fetch();
            affected.push_back({ f[0].Get<uint32>(), f[1].Get<std::string>() });
        } while (res->NextRow());

        if (affected.empty())
            return;

        // Drop the bounty rows up-front (single statement, then mail each).
        CharacterDatabase.DirectExecute(
            "DELETE FROM `character_nemesis_bounty` WHERE `target_spawn_id` = {}",
            uint32(targetSpawnId));

        uint32 const refundTokens = sConfigMgr->GetOption<uint32>(
            "NemesisSystem.BountyBoard.RefundTokensOnAdminClear", 1);
        uint32 const tokenItemId = GetRewardTokenItemId();

        // Subject (Russian): "Контракт отменён"
        static char const* const subject =
            "\u041a\u043e\u043d\u0442\u0440\u0430\u043a\u0442 "
            "\u043e\u0442\u043c\u0435\u043d\u0451\u043d";

        // Body variants (Russian). {} = nemesis title. Apologetic tone —
        // the player didn't fail, the target was removed externally.
        static char const* const bodies[] = {
            // "Цель контракта — {} — была устранена силами, неподвластными тебе.\n\nВот жетон в знак извинения."
            "\u0426\u0435\u043b\u044c \u043a\u043e\u043d\u0442\u0440\u0430\u043a\u0442\u0430 \u2014 {} \u2014 "
            "\u0431\u044b\u043b\u0430 \u0443\u0441\u0442\u0440\u0430\u043d\u0435\u043d\u0430 \u0441\u0438\u043b\u0430\u043c\u0438, "
            "\u043d\u0435\u043f\u043e\u0434\u0432\u043b\u0430\u0441\u0442\u043d\u044b\u043c\u0438 \u0442\u0435\u0431\u0435.\n\n"
            "\u0412\u043e\u0442 \u0436\u0435\u0442\u043e\u043d \u0432 \u0437\u043d\u0430\u043a "
            "\u0438\u0437\u0432\u0438\u043d\u0435\u043d\u0438\u044f.",
            // "Прости, путник. {} уже не представляет опасности — кто-то опередил тебя.\n\nПрими жетон за хлопоты."
            "\u041f\u0440\u043e\u0441\u0442\u0438, \u043f\u0443\u0442\u043d\u0438\u043a. {} \u0443\u0436\u0435 "
            "\u043d\u0435 \u043f\u0440\u0435\u0434\u0441\u0442\u0430\u0432\u043b\u044f\u0435\u0442 "
            "\u043e\u043f\u0430\u0441\u043d\u043e\u0441\u0442\u0438 \u2014 \u043a\u0442\u043e-\u0442\u043e "
            "\u043e\u043f\u0435\u0440\u0435\u0434\u0438\u043b \u0442\u0435\u0431\u044f.\n\n"
            "\u041f\u0440\u0438\u043c\u0438 \u0436\u0435\u0442\u043e\u043d \u0437\u0430 "
            "\u0445\u043b\u043e\u043f\u043e\u0442\u044b.",
            // "Контракт на {} отозван. Благодарю за готовность — вот жетон в качестве компенсации."
            "\u041a\u043e\u043d\u0442\u0440\u0430\u043a\u0442 \u043d\u0430 {} \u043e\u0442\u043e\u0437\u0432\u0430\u043d. "
            "\u0411\u043b\u0430\u0433\u043e\u0434\u0430\u0440\u044e \u0437\u0430 \u0433\u043e\u0442\u043e\u0432\u043d\u043e\u0441\u0442\u044c "
            "\u2014 \u0432\u043e\u0442 \u0436\u0435\u0442\u043e\u043d \u0432 \u043a\u0430\u0447\u0435\u0441\u0442\u0432\u0435 "
            "\u043a\u043e\u043c\u043f\u0435\u043d\u0441\u0430\u0446\u0438\u0438.",
        };

        ObjectGuid::LowType const senderEntry = MAIL_SENDER_ENTRY;

        for (auto const& a : affected)
        {
            uint32 const bodyIdx = urand(0, uint32(sizeof(bodies) / sizeof(bodies[0]) - 1));
            std::string body = Acore::StringFormat(bodies[bodyIdx], a.title);

            ObjectGuid const recvGuid = ObjectGuid::Create<HighGuid::Player>(a.guid);

            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            MailDraft draft(subject, body);

            if (refundTokens > 0 && tokenItemId)
            {
                Player const* online = ObjectAccessor::FindConnectedPlayer(recvGuid);
                if (Item* item = Item::CreateItem(tokenItemId, refundTokens, online))
                {
                    item->SaveToDB(trans);
                    draft.AddItem(item);
                }
            }

            draft.SendMailTo(trans,
                MailReceiver(a.guid),
                MailSender(MAIL_CREATURE, uint32(senderEntry)),
                MAIL_CHECK_MASK_NONE, 0);

            CharacterDatabase.CommitTransaction(trans);

            // Best-effort chat notification if the player is online.
            if (Player* online = ObjectAccessor::FindConnectedPlayer(recvGuid))
            {
                ChatHandler(online->GetSession()).PSendSysMessage(
                    // "[Немезида]: Контракт на {} отменён (цель устранена)."
                    "[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: "
                    "\xD0\x9A\xD0\xBE\xD0\xBD\xD1\x82\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82 \xD0\xBD\xD0\xB0 {} \xD0\xBE\xD1\x82\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x91\xD0\xBD "
                    "(\xD1\x86\xD0\xB5\xD0\xBB\xD1\x8C \xD1\x83\xD1\x81\xD1\x82\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xB0).",
                    a.title);
            }
        }
    }

    // Called from OnPlayerCreatureKill after the regular reward is granted.
    // If this kill matches the player's active bounty, awards the contract
    // tokens/gold, logs to history, and sends a completion mail. Returns
    // true if completion was processed (for announcement).
    // Sends the completion reward via RP-flavored mail: thank-you text
    // referencing the slain nemesis by its unique title, with tokens and
    // gold attached. Variant text is picked at random for variety.
    void SendBountyCompletionMail(Player* player, std::string const& title,
                                  uint32 tokenItemId, uint32 tokens, uint32 gold)
    {
        if (!player)
            return;
        if ((!tokenItemId || !tokens) && !gold)
            return;

        // Subject variants (Russian).
        static char const* const subjects[] = {
            // "Благодарность охотнику"
            "\u0411\u043b\u0430\u0433\u043e\u0434\u0430\u0440\u043d\u043e\u0441\u0442\u044c "
            "\u043e\u0445\u043e\u0442\u043d\u0438\u043a\u0443",
            // "Контракт выполнен"
            "\u041a\u043e\u043d\u0442\u0440\u0430\u043a\u0442 "
            "\u0432\u044b\u043f\u043e\u043b\u043d\u0435\u043d",
            // "За твою службу"
            "\u0417\u0430 \u0442\u0432\u043e\u044e \u0441\u043b\u0443\u0436\u0431\u0443",
            // "Слово трактирщика"
            "\u0421\u043b\u043e\u0432\u043e \u0442\u0440\u0430\u043a\u0442\u0438\u0440\u0449\u0438\u043a\u0430",
        };

        // Body variants (Russian). {} = nemesis title.
        static char const* const bodies[] = {
            // "Спасибо, путник. Наконец-то {} больше не омрачает нашу землю.
            //  Прими заслуженную награду. — Трактирщик"
            "\u0421\u043f\u0430\u0441\u0438\u0431\u043e, \u043f\u0443\u0442\u043d\u0438\u043a. "
            "\u041d\u0430\u043a\u043e\u043d\u0435\u0446-\u0442\u043e {} \u0431\u043e\u043b\u044c\u0448\u0435 "
            "\u043d\u0435 \u043e\u043c\u0440\u0430\u0447\u0430\u0435\u0442 \u043d\u0430\u0448\u0443 "
            "\u0437\u0435\u043c\u043b\u044e.\n\n\u041f\u0440\u0438\u043c\u0438 \u0437\u0430\u0441\u043b\u0443\u0436\u0435\u043d\u043d\u0443\u044e "
            "\u043d\u0430\u0433\u0440\u0430\u0434\u0443.\n\n\u2014 \u0422\u0440\u0430\u043a\u0442\u0438\u0440\u0449\u0438\u043a",

            // "Твой меч оказался тем, что нам нужно.
            //  {} повержен. Держи, что обещал."
            "\u0422\u0432\u043e\u0439 \u043c\u0435\u0447 \u043e\u043a\u0430\u0437\u0430\u043b\u0441\u044f "
            "\u0442\u0435\u043c, \u0447\u0442\u043e \u043d\u0430\u043c \u043d\u0443\u0436\u043d\u043e.\n\n"
            "{} \u043f\u043e\u0432\u0435\u0440\u0436\u0435\u043d. \u0414\u0435\u0440\u0436\u0438, "
            "\u0447\u0442\u043e \u043e\u0431\u0435\u0449\u0430\u043b.",

            // "Новости о гибели {} уже разошлись по округе.
            //  Прими благодарность — и награду."
            "\u041d\u043e\u0432\u043e\u0441\u0442\u0438 \u043e \u0433\u0438\u0431\u0435\u043b\u0438 {} "
            "\u0443\u0436\u0435 \u0440\u0430\u0437\u043e\u0448\u043b\u0438\u0441\u044c \u043f\u043e "
            "\u043e\u043a\u0440\u0443\u0433\u0435.\n\n\u041f\u0440\u0438\u043c\u0438 "
            "\u0431\u043b\u0430\u0433\u043e\u0434\u0430\u0440\u043d\u043e\u0441\u0442\u044c \u2014 "
            "\u0438 \u043d\u0430\u0433\u0440\u0430\u0434\u0443.",

            // "Немногие решались выйти на {}. Ты справился, герой. Забирай своё."
            "\u041d\u0435\u043c\u043d\u043e\u0433\u0438\u0435 \u0440\u0435\u0448\u0430\u043b\u0438\u0441\u044c "
            "\u0432\u044b\u0439\u0442\u0438 \u043d\u0430 {}. \u0422\u044b \u0441\u043f\u0440\u0430\u0432\u0438\u043b\u0441\u044f, "
            "\u0433\u0435\u0440\u043e\u0439.\n\n\u0417\u0430\u0431\u0438\u0440\u0430\u0439 \u0441\u0432\u043e\u0451.",

            // "Слава идёт впереди тебя. {} больше не тревожит наши сны.
            //  Награда твоя."
            "\u0421\u043b\u0430\u0432\u0430 \u0438\u0434\u0451\u0442 \u0432\u043f\u0435\u0440\u0435\u0434\u0438 "
            "\u0442\u0435\u0431\u044f. {} \u0431\u043e\u043b\u044c\u0448\u0435 \u043d\u0435 "
            "\u0442\u0440\u0435\u0432\u043e\u0436\u0438\u0442 \u043d\u0430\u0448\u0438 \u0441\u043d\u044b.\n\n"
            "\u041d\u0430\u0433\u0440\u0430\u0434\u0430 \u0442\u0432\u043e\u044f.",
        };

        uint32 const subjectIdx = urand(0, uint32(sizeof(subjects) / sizeof(subjects[0]) - 1));
        uint32 const bodyIdx = urand(0, uint32(sizeof(bodies) / sizeof(bodies[0]) - 1));

        std::string subject = subjects[subjectIdx];
        std::string body = Acore::StringFormat(bodies[bodyIdx], title);

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        MailDraft draft(subject, body);

        if (tokenItemId && tokens)
        {
            if (Item* item = Item::CreateItem(tokenItemId, tokens, player))
            {
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
        }

        if (gold)
            draft.AddMoney(gold);

        draft.SendMailTo(trans,
            MailReceiver(player, player->GetGUID().GetCounter()),
            MailSender(MAIL_CREATURE, MAIL_SENDER_ENTRY),
            MAIL_CHECK_MASK_NONE, 0);

        CharacterDatabase.CommitTransaction(trans);
    }

    bool CheckCompletion(Player* player, Creature* killed, std::string& outTitle, uint8& outRank)
    {
        if (!player || !killed)
            return false;

        ActiveBounty active;
        if (!GetActiveBounty(player, active))
            return false;

        // Direct spawnId match (normal nemesis case).
        bool matches = (uint32(killed->GetSpawnId()) == active.targetSpawnId);

        // Rare-mob match by creature entry: if the bounty's stored nemesis
        // state is a rare, the rare may currently be active at a *different*
        // pool spawn point than the one the bounty was accepted on. Accept
        // the kill as long as the creature entry matches.
        if (!matches)
        {
            auto stateIt = ActiveNemeses.find(active.targetSpawnId);
            if (stateIt != ActiveNemeses.end()
                && IsRareEntry(stateIt->second.creatureEntry)
                && stateIt->second.creatureEntry == killed->GetEntry())
            {
                matches = true;
            }
        }

        if (!matches)
            return false;

        // Resolve rank from current state (look up by the bounty's stored
        // spawnId for rares — killed->GetSpawnId() might differ).
        uint8 rank = 1;
        auto it = ActiveNemeses.find(active.targetSpawnId);
        if (it == ActiveNemeses.end())
            it = ActiveNemeses.find(killed->GetSpawnId());
        if (it != ActiveNemeses.end())
            rank = it->second.rank;

        uint32 const tokens = GetRewardTokens(rank);
        uint32 const gold = GetRewardGold(rank);
        uint32 const tokenItemId = GetRewardTokenItemId();

        // Reward is delivered via RP-flavored mail (subject + thank-you body
        // referencing the fallen nemesis by title, tokens + gold attached).
        SendBountyCompletionMail(player, active.targetTitle, tokenItemId, tokens, gold);

        // History log.
        uint32 const now = uint32(GameTime::GetGameTime().count());
        std::string title = active.targetTitle;
        CharacterDatabase.EscapeString(title);
        CharacterDatabase.Execute(
            "INSERT INTO `character_nemesis_bounty_history` "
            "(`guid`, `target_spawn_id`, `target_title`, `zone_id`, `target_rank`, `completed_at`, `tokens_earned`) "
            "VALUES ({}, {}, '{}', {}, {}, {}, {})",
            player->GetGUID().GetCounter(), active.targetSpawnId, title,
            active.zoneId, uint32(rank), now, tokens);

        // Clear active bounty (synchronous so a rapid follow-up lookup won't
        // see the stale row and try to double-credit).
        CharacterDatabase.DirectExecute(
            "DELETE FROM `character_nemesis_bounty` WHERE `guid` = {}",
            player->GetGUID().GetCounter());

        outTitle = active.targetTitle;
        outRank = rank;

        AnnounceCompletion(player, active.targetTitle);
        return true;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Hunter's Covenant — reputation earned from nemesis kills + bounty contracts.
// Server-only counter (no real Blizzard faction). Grants custom CharTitles at
// each tier threshold. Titles are cumulative — earning rank 3 also keeps the
// rank 1 and 2 titles so players can pick any they've earned.
// ────────────────────────────────────────────────────────────────────────────
namespace NemesisReputation
{
    // DBC IDs for the 5 rank titles (see CharTitles_custom.csv + matching
    // chartitles_dbc SQL overlay). Mask_ID == ID for each row.
    // Must stay below MAX_TITLE_INDEX (192) — the server's KnownTitles field
    // is 3 uint64 = 192 bits wide. Stock WotLK uses up to Mask_ID 142.
    static constexpr uint32 RANK_TITLES[5] = { 180, 181, 182, 183, 184 };

    struct Cache
    {
        uint32 points = 0;
        uint8  highestRank = 0;  // 0 = no rank yet; grants begin at rank 1.
        bool   loaded = false;
    };

    // Keyed by player guid low. Populated lazily on first query; wiped on
    // logout. Write-through: each AddPoints also persists via REPLACE INTO.
    static std::unordered_map<uint32, Cache> PlayerRep;

    uint32 GetThreshold(uint8 rank)
    {
        // Rank 1 is the baseline (0 points). Thresholds are config-tunable.
        static constexpr uint32 DefaultThresholds[6] = { 0, 0, 500, 2500, 8000, 20000 };
        if (rank <= 1) return 0;
        if (rank > 5) return DefaultThresholds[5];
        static char const* const Keys[6] = {
            "", "",
            "NemesisRep.Threshold.Rank2",
            "NemesisRep.Threshold.Rank3",
            "NemesisRep.Threshold.Rank4",
            "NemesisRep.Threshold.Rank5",
        };
        return sConfigMgr->GetOption<uint32>(Keys[rank], DefaultThresholds[rank]);
    }

    uint8 ComputeRank(uint32 points)
    {
        for (uint8 r = 5; r >= 2; --r)
            if (points >= GetThreshold(r))
                return r;
        return 1;
    }

    Cache& Load(Player* player)
    {
        uint32 const guidLow = player->GetGUID().GetCounter();
        Cache& c = PlayerRep[guidLow];
        if (c.loaded)
            return c;
        c.loaded = true;

        QueryResult result = CharacterDatabase.Query(
            "SELECT `points`, `highest_rank` FROM `character_nemesis_reputation` "
            "WHERE `guid` = {}", guidLow);
        if (result)
        {
            Field* f = result->Fetch();
            c.points = f[0].Get<uint32>();
            c.highestRank = f[1].Get<uint8>();
            // Legacy rows in the migration defaulted highest_rank to 1 —
            // treat anything >=1 as "has earned rank 1".
        }
        return c;
    }

    uint32 GetPoints(Player* player)
    {
        if (!player) return 0;
        return Load(player).points;
    }

    // Returns the rank for display — minimum 1 so UI never shows "rank 0".
    uint8 GetRank(Player* player)
    {
        if (!player) return 1;
        uint8 const r = Load(player).highestRank;
        return r < 1 ? 1 : r;
    }

    // Russian rank names — used in rank-up chat announcement. Written as UTF-8
    // via \u escapes so source stays ASCII-clean in this section.
    char const* GetRankName(uint8 rank)
    {
        switch (rank)
        {
            case 1: return "\u041f\u043e\u0441\u043b\u0443\u0448\u043d\u0438\u043a";          // Послушник
            case 2: return "\u041e\u0445\u043e\u0442\u043d\u0438\u043a";                      // Охотник
            case 3: return "\u0421\u043b\u0435\u0434\u043e\u043f\u044b\u0442";                // Следопыт
            case 4: return "\u0412\u0435\u0442\u0435\u0440\u0430\u043d \u041e\u0445\u043e\u0442\u044b";  // Ветеран Охоты
            case 5: return "\u041b\u0435\u0433\u0435\u043d\u0434\u0430 \u041e\u0445\u043e\u0442\u044b";  // Легенда Охоты
        }
        return "";
    }

    // Sets the title bit on the player's KnownTitles mask. Cumulative — the
    // player keeps all lower-tier titles they've previously earned.
    void GrantTitle(Player* player, uint8 rank)
    {
        if (!player || rank < 1 || rank > 5) return;
        CharTitlesEntry const* entry = sCharTitlesStore.LookupEntry(RANK_TITLES[rank - 1]);
        if (!entry) return;
        player->SetTitle(entry, false);
    }

    // Awards `amount` points. On crossing one or more thresholds, grants the
    // corresponding CharTitle(s) and emits a [Немезида] rank-up announcement.
    // Returns true iff a rank-up occurred.
    bool AddPoints(Player* player, uint32 amount)
    {
        if (!player || amount == 0) return false;
        if (!sConfigMgr->GetOption<bool>("NemesisRep.Enable", true)) return false;

        Cache& c = Load(player);
        c.points += amount;

        uint8 const newRank = ComputeRank(c.points);
        bool rankedUp = false;

        if (newRank > c.highestRank)
        {
            // Multi-step leap: grant every newly crossed title.
            for (uint8 r = c.highestRank + 1; r <= newRank; ++r)
                GrantTitle(player, r);
            c.highestRank = newRank;
            rankedUp = true;

            ChatHandler(player->GetSession()).PSendSysMessage(
                // "[Немезида]: Новый ранг — {}."
                "[\u041d\u0435\u043c\u0435\u0437\u0438\u0434\u0430]: "
                "\u041d\u043e\u0432\u044b\u0439 \u0440\u0430\u043d\u0433 \u2014 {}.",
                GetRankName(newRank));
        }

        // Write-through. REPLACE INTO is ~free and avoids loss on crash/logout.
        uint32 const now = uint32(GameTime::GetGameTime().count());
        CharacterDatabase.Execute(
            "REPLACE INTO `character_nemesis_reputation` "
            "(`guid`, `points`, `highest_rank`, `updated_at`) "
            "VALUES ({}, {}, {}, {})",
            player->GetGUID().GetCounter(), c.points, c.highestRank, now);

        return rankedUp;
    }

    // On login, set every title bit for ranks the player has already earned.
    // Covers two cases: (a) legacy grants that ran before chartitles_dbc was
    // populated — sCharTitlesStore.LookupEntry returned null and the bit was
    // silently skipped; (b) future migrations that add new ranks below what
    // the player already holds.
    void OnLogin(Player* player)
    {
        if (!player) return;
        Cache& c = Load(player);
        if (c.highestRank < 1) return;  // no rep row yet, no titles to grant
        for (uint8 r = 1; r <= c.highestRank; ++r)
            GrantTitle(player, r);
    }

    void OnLogout(Player* player)
    {
        if (!player) return;
        PlayerRep.erase(player->GetGUID().GetCounter());
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Bounty Vendor — adds "Bounty Hunter Rewards" gossip option to all innkeepers
// ────────────────────────────────────────────────────────────────────────────

// Send a custom string as the gossip "header text". Works by sending
// SMSG_NPC_TEXT_UPDATE with our text before the gossip menu — the client
// caches it for the given textId and renders it above the gossip options.
// Pick a textId unique per player to avoid cache collisions on the client.
static void SendCustomNpcText(Player* player, std::string const& text, uint32 textId)
{
    if (!player || !player->GetSession())
        return;

    WorldPacket data(SMSG_NPC_TEXT_UPDATE, 100);
    data << uint32(textId);

    // 8 text options expected by the client. Put our text in option 0
    // (probability 1.0), leave the rest with probability 0 and empty text.
    for (uint8 i = 0; i < 8; ++i)
    {
        data << float(i == 0 ? 1.0f : 0.0f);
        data << (i == 0 ? text : std::string());  // male/default
        data << (i == 0 ? text : std::string());  // female fallback
        data << uint32(0);                         // language (universal)
        for (uint8 j = 0; j < 3; ++j)              // 3 emote slots
        {
            data << uint32(0);                     // delay
            data << uint32(0);                     // emote
        }
    }

    player->GetSession()->SendPacket(&data);
}

class NemesisBountyVendorScript : public AllCreatureScript
{
    // Innkeeper gossip constants (mirrors npc_innkeeper.cpp)
    static constexpr uint32 INNKEEPER_GOSSIP_MENU    = 9733;
    static constexpr uint32 INNKEEPER_GOSSIP_EVENT   = 342;
    static constexpr uint32 SPELL_TRICK              = 24714;
    static constexpr uint32 SPELL_TREAT              = 24715;
    static constexpr uint32 SPELL_TRICKED_OR_TREATED = 24755;
    static constexpr uint32 HALLOWEEN_EVENTID        = 12;

    // Our custom actions
    static constexpr uint32 BOUNTY_GOSSIP_ACTION       = GOSSIP_ACTION_INFO_DEF + 9000;
    static constexpr uint32 BOARD_GOSSIP_ACTION        = GOSSIP_ACTION_INFO_DEF + 9001;
    static constexpr uint32 BOARD_BACK_ACTION          = GOSSIP_ACTION_INFO_DEF + 9002;
    static constexpr uint32 BOARD_ACTIVE_VIEW_ACTION   = GOSSIP_ACTION_INFO_DEF + 9003;
    static constexpr uint32 BOARD_ABANDON_ACTION       = GOSSIP_ACTION_INFO_DEF + 9004;
    // Rank-gated shop submenus (2026-06-07): rank 1 general goods, rank 2
    // StatBooster consumables, rank 3 familiar gacha bags.
    static constexpr uint32 SHOP_GENERAL_ACTION        = GOSSIP_ACTION_INFO_DEF + 9005;
    static constexpr uint32 SHOP_STATBOOST_ACTION      = GOSSIP_ACTION_INFO_DEF + 9006;
    static constexpr uint32 SHOP_FAMILIAR_ACTION       = GOSSIP_ACTION_INFO_DEF + 9007;
    static constexpr uint32 SHOP_BACK_ACTION           = GOSSIP_ACTION_INFO_DEF + 9008;
    // Slot-indexed ranges (room for 100 slots; SlotsPerDay default is 3).
    static constexpr uint32 BOARD_DETAIL_BASE_ACTION   = GOSSIP_ACTION_INFO_DEF + 9100;  // +slot
    static constexpr uint32 BOARD_ACCEPT_BASE_ACTION   = GOSSIP_ACTION_INFO_DEF + 9300;  // +slot

public:
    NemesisBountyVendorScript() : AllCreatureScript("NemesisBountyVendorScript") { }

    bool CanCreatureGossipHello(Player* player, Creature* creature) override
    {
        if (!creature->IsInnkeeper())
            return false;

        if (!sConfigMgr->GetOption<bool>("NemesisSystem.Enable", false))
            return false;

        if (!sConfigMgr->GetOption<bool>("NemesisSystem.BountyVendor.Enable", true))
            return false;

        // Replicate npc_innkeeper OnGossipHello + add bounty option
        ClearGossipMenuFor(player);

        // Halloween event option
        if (IsEventActive(HALLOWEEN_EVENTID) && !player->HasAura(SPELL_TRICKED_OR_TREATED))
            AddGossipItemFor(player, INNKEEPER_GOSSIP_EVENT, 0,
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + HALLOWEEN_EVENTID);

        // Quest giver
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        // Vendor (existing innkeeper vendor items)
        if (creature->IsVendor())
            AddGossipItemFor(player, INNKEEPER_GOSSIP_MENU, 2,
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

        // Make this inn your home
        if (creature->IsInnkeeper())
            AddGossipItemFor(player, INNKEEPER_GOSSIP_MENU, 1,
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INN);

        // ── Bounty Hunter Rewards ──
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
            "\u041d\u0430\u0433\u0440\u0430\u0434\u044b \u043e\u0445\u043e\u0442\u043d\u0438\u043a\u0430 \u0437\u0430 \u0433\u043e\u043b\u043e\u0432\u0430\u043c\u0438",  // Награды охотника за головами
            GOSSIP_SENDER_MAIN, BOUNTY_GOSSIP_ACTION);

        // ── Bounty Board (contracts) ──
        if (NemesisBountyBoard::IsEnabled())
            AddGossipItemFor(player, GOSSIP_ICON_TAXI,
                "\u0414\u043e\u0441\u043a\u0430 \u043e\u0431\u044a\u044f\u0432\u043b\u0435\u043d\u0438\u0439 \u043e\u0445\u043e\u0442\u043d\u0438\u043a\u0430 \u0437\u0430 \u0433\u043e\u043b\u043e\u0432\u0430\u043c\u0438",  // Доска объявлений охотника за головами
                GOSSIP_SENDER_MAIN, BOARD_GOSSIP_ACTION);

        player->TalkedToCreature(creature->GetEntry(), creature->GetGUID());
        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    bool CanCreatureGossipSelect(Player* player, Creature* creature,
        uint32 sender, uint32 action) override
    {
        if (!creature->IsInnkeeper())
            return false;

        if (!sConfigMgr->GetOption<bool>("NemesisSystem.Enable", false))
            return false;

        if (!sConfigMgr->GetOption<bool>("NemesisSystem.BountyVendor.Enable", true))
            return false;

        ClearGossipMenuFor(player);

        // Halloween trick-or-treat
        if (action == GOSSIP_ACTION_INFO_DEF + HALLOWEEN_EVENTID
            && IsEventActive(HALLOWEEN_EVENTID)
            && !player->HasAura(SPELL_TRICKED_OR_TREATED))
        {
            player->CastSpell(player, SPELL_TRICKED_OR_TREATED, true);
            creature->CastSpell(player, roll_chance_i(50) ? SPELL_TRICK : SPELL_TREAT, true);
            CloseGossipMenuFor(player);
            return true;
        }

        // ── Bounty vendor: rank-gated shop submenu ──
        if (action == BOUNTY_GOSSIP_ACTION)
        {
            if (sConfigMgr->GetOption<bool>("NemesisSystem.BountyVendor.RankMenus.Enable", true))
            {
                ShowShopMenu(player, creature);
                return true;
            }

            // Legacy flat vendor (RankMenus disabled)
            OpenVendor(player, creature, sConfigMgr->GetOption<uint32>(
                "NemesisSystem.BountyVendor.Entry", 190000));
            return true;
        }

        // ── Shop submenu: back to the innkeeper root menu ──
        if (action == SHOP_BACK_ACTION)
            return CanCreatureGossipHello(player, creature);

        // ── Shop submenu: general goods (rank 1, no gate) ──
        if (action == SHOP_GENERAL_ACTION)
        {
            OpenVendor(player, creature, sConfigMgr->GetOption<uint32>(
                "NemesisSystem.BountyVendor.GeneralEntry", 190100));
            return true;
        }

        // ── Shop submenu: StatBooster consumables / familiar bags (gated) ──
        if (action == SHOP_STATBOOST_ACTION || action == SHOP_FAMILIAR_ACTION)
        {
            bool const familiar = (action == SHOP_FAMILIAR_ACTION);
            uint8 const need = ShopRankRequired(familiar);
            if (NemesisReputation::GetRank(player) < need)
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    // "[Немезида]: Эти товары доступны с ранга «{}»."
                    "[\u041d\u0435\u043c\u0435\u0437\u0438\u0434\u0430]: "
                    "\u042d\u0442\u0438 \u0442\u043e\u0432\u0430\u0440\u044b \u0434\u043e\u0441\u0442\u0443\u043f\u043d\u044b "
                    "\u0441 \u0440\u0430\u043d\u0433\u0430 \u00ab{}\u00bb.",
                    NemesisReputation::GetRankName(need));
                ShowShopMenu(player, creature);
                return true;
            }

            OpenVendor(player, creature, familiar
                ? sConfigMgr->GetOption<uint32>("NemesisSystem.BountyVendor.FamiliarEntry", 190102)
                : sConfigMgr->GetOption<uint32>("NemesisSystem.BountyVendor.StatBoosterEntry", 190101));
            return true;
        }

        // ── Bounty board: main view ──
        if (action == BOARD_GOSSIP_ACTION || action == BOARD_BACK_ACTION)
        {
            ShowBoard(player, creature);
            return true;
        }

        // ── Bounty board: active bounty view ──
        if (action == BOARD_ACTIVE_VIEW_ACTION)
        {
            ShowActiveBounty(player, creature);
            return true;
        }

        // ── Bounty board: abandon active bounty ──
        if (action == BOARD_ABANDON_ACTION)
        {
            NemesisBountyBoard::AbandonBounty(player);
            ChatHandler(player->GetSession()).PSendSysMessage(
                // "Контракт отменён."
                "[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: "
                "\xD0\x9A\xD0\xBE\xD0\xBD\xD1\x82\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82 \xD0\xBE\xD1\x82\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x91\xD0\xBD.");
            ShowBoard(player, creature);
            return true;
        }

        // ── Bounty board: click pool bounty → details ──
        if (action >= BOARD_DETAIL_BASE_ACTION && action < BOARD_DETAIL_BASE_ACTION + 100)
        {
            uint32 slot = action - BOARD_DETAIL_BASE_ACTION;
            ShowBountyDetail(player, creature, slot);
            return true;
        }

        // ── Bounty board: confirm accept ──
        if (action >= BOARD_ACCEPT_BASE_ACTION && action < BOARD_ACCEPT_BASE_ACTION + 100)
        {
            uint32 slot = action - BOARD_ACCEPT_BASE_ACTION;

            // Block accepting a new contract while one is active.
            NemesisBountyBoard::ActiveBounty existing;
            if (NemesisBountyBoard::GetActiveBounty(player, existing))
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    // "У вас уже есть активный контракт. Откажитесь сначала."
                    "[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: "
                    "\xD0\xA3 \xD0\xB2\xD0\xB0\xD1\x81 \xD1\x83\xD0\xB6\xD0\xB5 \xD0\xB5\xD1\x81\xD1\x82\xD1\x8C \xD0\xB0\xD0\xBA\xD1\x82\xD0\xB8\xD0\xB2\xD0\xBD\xD1\x8B\xD0\xB9 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x82\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82.");
                ShowActiveBounty(player, creature);
                return true;
            }

            // Session-cap guard — can't exceed SlotsPerDay completions per
            // 2h window IN THIS ZONE (matches ShowBoard's per-zone cap).
            uint32 const capWindowStart = NemesisBountyBoard::GetLastDailyResetTs();
            uint32 capCompletions = 0;
            if (QueryResult r = CharacterDatabase.Query(
                "SELECT COUNT(*) FROM `character_nemesis_bounty_history` "
                "WHERE `guid` = {} AND `zone_id` = {} AND `completed_at` >= {}",
                player->GetGUID().GetCounter(), creature->GetZoneId(), capWindowStart))
            {
                capCompletions = r->Fetch()[0].Get<uint32>();
            }
            if (capCompletions >= NemesisBountyBoard::GetSlotsPerDay())
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    // "[Немезида]: На сегодня с тебя хватит. Возвращайся позже."
                    "[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: "
                    "\xD0\x9D\xD0\xB0 \xD1\x81\xD0\xB5\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xBD\xD1\x8F \xD1\x81 \xD1\x82\xD0\xB5\xD0\xB1\xD1\x8F \xD1\x85\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB8\xD1\x82. "
                    "\xD0\x92\xD0\xBE\xD0\xB7\xD0\xB2\xD1\x80\xD0\xB0\xD1\x89\xD0\xB0\xD0\xB9\xD1\x81\xD1\x8F \xD0\xBF\xD0\xBE\xD0\xB7\xD0\xB6\xD0\xB5.");
                ShowBoard(player, creature);
                return true;
            }

            NemesisBountyBoard::DailyPool const& pool =
                NemesisBountyBoard::GetOrGeneratePool(player, creature->GetZoneId());
            if (slot >= pool.bounties.size())
            {
                ShowBoard(player, creature);
                return true;
            }

            auto const& b = pool.bounties[slot];

            // Stale-pool guard: same check as ShowBoard — if the target has
            // moved zones (rare pool rotation) or was cleared, don't accept.
            auto bountyIt = ActiveNemeses.find(b.spawnId);
            if (bountyIt == ActiveNemeses.end() || bountyIt->second.zoneId != creature->GetZoneId())
            {
                ChatHandler(player->GetSession()).PSendSysMessage(
                    // "[Немезида]: Цель ускользнула. Попробуй другой контракт."
                    "[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: "
                    "\xD0\xA6\xD0\xB5\xD0\xBB\xD1\x8C \xD1\x83\xD1\x81\xD0\xBA\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBD\xD1\x83\xD0\xBB\xD0\xB0. \xD0\x9F\xD0\xBE\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB1\xD1\x83\xD0\xB9 \xD0\xB4\xD1\x80\xD1\x83\xD0\xB3\xD0\xBE\xD0\xB9 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x82\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82.");
                ShowBoard(player, creature);
                return true;
            }

            NemesisBountyBoard::AcceptBounty(player, b, creature->GetZoneId());

            uint32 const tokens = NemesisBountyBoard::GetRewardTokens(b.rank);
            ChatHandler(player->GetSession()).PSendSysMessage(
                // "Контракт принят: {} (награда {} жет.)"
                "[\xD0\x9D\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xB7\xD0\xB8\xD0\xB4\xD0\xB0]: "
                "\xD0\x9A\xD0\xBE\xD0\xBD\xD1\x82\xD1\x80\xD0\xB0\xD0\xBA\xD1\x82 \xD0\xBF\xD1\x80\xD0\xB8\xD0\xBD\xD1\x8F\xD1\x82: {} (\xD0\xBD\xD0\xB0\xD0\xB3\xD1\x80\xD0\xB0\xD0\xB4\xD0\xB0 {} \xD0\xB6\xD0\xB5\xD1\x82.)",
                b.title, tokens);

            ShowBoard(player, creature);
            return true;
        }

        // Default innkeeper actions
        CloseGossipMenuFor(player);

        switch (action)
        {
            case GOSSIP_ACTION_TRADE:
                player->GetSession()->SendListInventory(creature->GetGUID());
                break;
            case GOSSIP_ACTION_INN:
                player->SetBindPoint(creature->GetGUID());
                break;
        }

        return true;
    }

private:
    // Required reputation rank for the gated shop submenus (config-tunable).
    static uint8 ShopRankRequired(bool familiar)
    {
        return uint8(familiar
            ? sConfigMgr->GetOption<uint32>("NemesisSystem.BountyVendor.FamiliarRank", 3)
            : sConfigMgr->GetOption<uint32>("NemesisSystem.BountyVendor.StatBoosterRank", 2));
    }

    // Show a vendor list by virtual npc_vendor entry. Temporarily grants the
    // vendor flag so SendListInventory succeeds on non-vendor innkeepers.
    static void OpenVendor(Player* player, Creature* creature, uint32 vendorEntry)
    {
        bool const hadVendorFlag = creature->HasNpcFlag(UNIT_NPC_FLAG_VENDOR);
        if (!hadVendorFlag)
            creature->SetNpcFlag(UNIT_NPC_FLAG_VENDOR);

        player->GetSession()->SendListInventory(creature->GetGUID(), vendorEntry);

        if (!hadVendorFlag)
            creature->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);
    }

    // Rank-gated shop submenu: «Общие товары» (rank 1) / «Печати и нашивки»
    // (StatBooster, rank 2) / «Сумки с фамильярами» (rank 3). Locked entries
    // stay visible with a "(требуется ранг: …)" suffix so players see the
    // progression; clicking one reports the missing rank.
    void ShowShopMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        uint8 const rank = NemesisReputation::GetRank(player);

        // "Общие товары"
        AddGossipItemFor(player, GOSSIP_ICON_VENDOR,
            "\u041e\u0431\u0449\u0438\u0435 \u0442\u043e\u0432\u0430\u0440\u044b",
            GOSSIP_SENDER_MAIN, SHOP_GENERAL_ACTION);

        // "Печати и нашивки" (StatBooster)
        {
            std::string label = "\u041f\u0435\u0447\u0430\u0442\u0438 \u0438 \u043d\u0430\u0448\u0438\u0432\u043a\u0438";
            if (uint8 const need = ShopRankRequired(false); rank < need)
                label += Acore::StringFormat(
                    " (\u0442\u0440\u0435\u0431\u0443\u0435\u0442\u0441\u044f \u0440\u0430\u043d\u0433: {})",
                    NemesisReputation::GetRankName(need));
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, label,
                GOSSIP_SENDER_MAIN, SHOP_STATBOOST_ACTION);
        }

        // "Сумки с фамильярами"
        {
            std::string label = "\u0421\u0443\u043c\u043a\u0438 \u0441 \u0444\u0430\u043c\u0438\u043b\u044c\u044f\u0440\u0430\u043c\u0438";
            if (uint8 const need = ShopRankRequired(true); rank < need)
                label += Acore::StringFormat(
                    " (\u0442\u0440\u0435\u0431\u0443\u0435\u0442\u0441\u044f \u0440\u0430\u043d\u0433: {})",
                    NemesisReputation::GetRankName(need));
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, label,
                GOSSIP_SENDER_MAIN, SHOP_FAMILIAR_ACTION);
        }

        // "Назад"
        AddGossipItemFor(player, GOSSIP_ICON_CHAT,
            "\u041d\u0430\u0437\u0430\u0434",
            GOSSIP_SENDER_MAIN, SHOP_BACK_ACTION);

        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    }

    // Main board view. If the player has an active contract, hides the pool
    // and shows only the active bounty — the innkeeper refuses new work
    // until the current contract is resolved.
    void ShowBoard(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        NemesisBountyBoard::ActiveBounty active;
        bool const hasActive = NemesisBountyBoard::GetActiveBounty(player, active);

        if (hasActive)
        {
            // Innkeeper refuses new contracts while the player has one pending.
            // "Сначала разберись с прошлым делом, путник.
            //  У тебя уже есть контракт на {title}."
            std::string refusal = Acore::StringFormat(
                "\u0421\u043d\u0430\u0447\u0430\u043b\u0430 \u0440\u0430\u0437\u0431\u0435\u0440\u0438\u0441\u044c "
                "\u0441 \u043f\u0440\u043e\u0448\u043b\u044b\u043c \u0434\u0435\u043b\u043e\u043c, "
                "\u043f\u0443\u0442\u043d\u0438\u043a.\n\n"
                "\u0423 \u0442\u0435\u0431\u044f \u0443\u0436\u0435 \u0435\u0441\u0442\u044c "
                "\u043a\u043e\u043d\u0442\u0440\u0430\u043a\u0442 \u043d\u0430 {}.",
                active.targetTitle);

            uint32 const customTextId = 0x7F000000u | (player->GetGUID().GetCounter() & 0x00FFFFFFu);
            SendCustomNpcText(player, refusal, customTextId);

            uint32 const now = uint32(GameTime::GetGameTime().count());
            uint32 const remaining = active.expiresAt > now ? active.expiresAt - now : 0u;
            uint32 const hoursLeft = remaining / 3600u;
            uint32 const minsLeft = (remaining % 3600u) / 60u;

            // "[АКТИВНО] Title — осталось Xч Yм"
            std::string line = Acore::StringFormat(
                "[\u0410\u041a\u0422\u0418\u0412\u041d\u041e] {} \u2014 \u043e\u0441\u0442\u0430\u043b\u043e\u0441\u044c {}\u0447 {}\u043c",
                active.targetTitle, hoursLeft, minsLeft);
            AddGossipItemFor(player, GOSSIP_ICON_TAXI, line,
                GOSSIP_SENDER_MAIN, BOARD_ACTIVE_VIEW_ACTION);

            SendGossipMenuFor(player, customTextId, creature->GetGUID());
            return;
        }

        // No active bounty — show the daily pool for this zone.
        NemesisBountyBoard::DailyPool const& pool =
            NemesisBountyBoard::GetOrGeneratePool(player, creature->GetZoneId());

        if (pool.bounties.empty())
        {
            // "Нет доступных контрактов."
            AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                "\u041d\u0435\u0442 \u0434\u043e\u0441\u0442\u0443\u043f\u043d\u044b\u0445 \u043a\u043e\u043d\u0442\u0440\u0430\u043a\u0442\u043e\u0432.",
                GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
        }
        else
        {
            uint32 const innkeeperZone = creature->GetZoneId();

            // Bounties this player completed in THIS zone during the current
            // pool window are hidden until the window rolls. The cap is also
            // per-zone, so other zones have their own independent 3-slot
            // budget — think of each innkeeper's town as a separate contract.
            uint32 const windowStart = NemesisBountyBoard::GetLastDailyResetTs();
            std::unordered_set<uint32> completedThisWindow;
            if (QueryResult res = CharacterDatabase.Query(
                "SELECT `target_spawn_id` FROM `character_nemesis_bounty_history` "
                "WHERE `guid` = {} AND `zone_id` = {} AND `completed_at` >= {}",
                player->GetGUID().GetCounter(), innkeeperZone, windowStart))
            {
                do
                {
                    completedThisWindow.insert(res->Fetch()[0].Get<uint32>());
                } while (res->NextRow());
            }

            // Session cap: the player can accept at most SlotsPerDay contracts
            // per pool window (default 3 per 2h). Each completion consumes a
            // slot permanently for the window — even if new nemeses appear
            // later, only the remaining slots are offered.
            uint32 const totalSlots = NemesisBountyBoard::GetSlotsPerDay();
            uint32 const completedCount = uint32(completedThisWindow.size());
            uint32 const remainingSlots = completedCount >= totalSlots
                ? 0u : (totalSlots - completedCount);

            uint32 shown = 0;
            if (remainingSlots > 0)
            {
                for (size_t i = 0; i < pool.bounties.size() && shown < remainingSlots; ++i)
                {
                    auto const& b = pool.bounties[i];

                    // Stale-entry guard: nemesis moved (rare pool rotation) or
                    // was cleared since the pool was generated.
                    auto it = ActiveNemeses.find(b.spawnId);
                    if (it == ActiveNemeses.end() || it->second.zoneId != innkeeperZone)
                        continue;

                    // Already completed by this player in this pool window.
                    if (completedThisWindow.count(uint32(b.spawnId)))
                        continue;

                    uint32 const tokens = NemesisBountyBoard::GetRewardTokens(b.rank);

                    // Blizzlike compact: "[Р6] Title — 15ж"
                    std::string line = Acore::StringFormat(
                        "[\u0420{}] {} \u2014 {}\u0436",
                        uint32(b.rank), b.title, tokens);

                    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, line,
                        GOSSIP_SENDER_MAIN, BOARD_DETAIL_BASE_ACTION + uint32(i));
                    ++shown;
                }
            }

            if (shown == 0)
            {
                // Three distinct empty states:
                //  (a) session cap reached — player used all SlotsPerDay
                //      contracts this window → triumphant "well done, later"
                //  (b) zone has no nemeses at all → flat "no contracts"
                //  (c) pool has nemeses but all are stale/completed → same
                //      triumphant message as (a)
                bool const capReached = (remainingSlots == 0);
                bool const hadAny = !pool.bounties.empty();
                char const* emptyText = nullptr;

                if (!capReached && !hadAny)
                {
                    // "Нет доступных контрактов."
                    emptyText =
                        "\u041d\u0435\u0442 \u0434\u043e\u0441\u0442\u0443\u043f\u043d\u044b\u0445 "
                        "\u043a\u043e\u043d\u0442\u0440\u0430\u043a\u0442\u043e\u0432.";
                }
                else
                {
                    static char const* const clearedVariants[] = {
                        // "Ты оказался полезен. Возвращайся позже — новые заказы будут."
                        "\u0422\u044b \u043e\u043a\u0430\u0437\u0430\u043b\u0441\u044f \u043f\u043e\u043b\u0435\u0437\u0435\u043d. "
                        "\u0412\u043e\u0437\u0432\u0440\u0430\u0449\u0430\u0439\u0441\u044f \u043f\u043e\u0437\u0436\u0435 \u2014 "
                        "\u043d\u043e\u0432\u044b\u0435 \u0437\u0430\u043a\u0430\u0437\u044b \u0431\u0443\u0434\u0443\u0442.",
                        // "Ты очистил наши края, путник. Загляни в другую таверну — может, у них найдётся работа."
                        "\u0422\u044b \u043e\u0447\u0438\u0441\u0442\u0438\u043b \u043d\u0430\u0448\u0438 "
                        "\u043a\u0440\u0430\u044f, \u043f\u0443\u0442\u043d\u0438\u043a. "
                        "\u0417\u0430\u0433\u043b\u044f\u043d\u0438 \u0432 \u0434\u0440\u0443\u0433\u0443\u044e "
                        "\u0442\u0430\u0432\u0435\u0440\u043d\u0443 \u2014 \u043c\u043e\u0436\u0435\u0442, \u0443 \u043d\u0438\u0445 "
                        "\u043d\u0430\u0439\u0434\u0451\u0442\u0441\u044f \u0440\u0430\u0431\u043e\u0442\u0430.",
                        // "Спасибо за службу. Мне больше нечего тебе поручить — спроси в других краях."
                        "\u0421\u043f\u0430\u0441\u0438\u0431\u043e \u0437\u0430 \u0441\u043b\u0443\u0436\u0431\u0443. "
                        "\u041c\u043d\u0435 \u0431\u043e\u043b\u044c\u0448\u0435 \u043d\u0435\u0447\u0435\u0433\u043e "
                        "\u0442\u0435\u0431\u0435 \u043f\u043e\u0440\u0443\u0447\u0438\u0442\u044c \u2014 \u0441\u043f\u0440\u043e\u0441\u0438 "
                        "\u0432 \u0434\u0440\u0443\u0433\u0438\u0445 \u043a\u0440\u0430\u044f\u0445.",
                        // "Покуда здесь тихо. Может, у трактирщиков в других городах есть кто поопаснее."
                        "\u041f\u043e\u043a\u0443\u0434\u0430 \u0437\u0434\u0435\u0441\u044c \u0442\u0438\u0445\u043e. "
                        "\u041c\u043e\u0436\u0435\u0442, \u0443 \u0442\u0440\u0430\u043a\u0442\u0438\u0440\u0449\u0438\u043a\u043e\u0432 "
                        "\u0432 \u0434\u0440\u0443\u0433\u0438\u0445 \u0433\u043e\u0440\u043e\u0434\u0430\u0445 "
                        "\u0435\u0441\u0442\u044c \u043a\u0442\u043e \u043f\u043e\u043e\u043f\u0430\u0441\u043d\u0435\u0435.",
                        // "Отдохни, охотник. Когда появятся новые дела — я тебя позову."
                        "\u041e\u0442\u0434\u043e\u0445\u043d\u0438, \u043e\u0445\u043e\u0442\u043d\u0438\u043a. "
                        "\u041a\u043e\u0433\u0434\u0430 \u043f\u043e\u044f\u0432\u044f\u0442\u0441\u044f "
                        "\u043d\u043e\u0432\u044b\u0435 \u0434\u0435\u043b\u0430 \u2014 "
                        "\u044f \u0442\u0435\u0431\u044f \u043f\u043e\u0437\u043e\u0432\u0443.",
                    };
                    uint32 const idx = urand(0, uint32(sizeof(clearedVariants) / sizeof(clearedVariants[0]) - 1));
                    emptyText = clearedVariants[idx];
                }

                AddGossipItemFor(player, GOSSIP_ICON_CHAT, emptyText,
                    GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
            }
        }

        SendGossipMenuFor(player, player->GetGossipTextId(creature), creature->GetGUID());
    }

    // Detail view for a pool bounty — innkeeper whispers the details to
    // keep the gossip menu clean (Accept / Back only).
    void ShowBountyDetail(Player* player, Creature* creature, uint32 slot)
    {
        ClearGossipMenuFor(player);

        NemesisBountyBoard::DailyPool const& pool =
            NemesisBountyBoard::GetOrGeneratePool(player, creature->GetZoneId());

        if (slot >= pool.bounties.size())
        {
            ShowBoard(player, creature);
            return;
        }

        auto const& b = pool.bounties[slot];
        uint32 const tokens = NemesisBountyBoard::GetRewardTokens(b.rank);

        // Innkeeper's flavor text — blizzlike wanted-poster dialog in Russian.
        // (Zone omitted intentionally — the player is already standing in it.)
        // "Послушай меня, путник...
        //  В наших краях лютует {title} (ранг {rank}).
        //  Принеси весть о его гибели — и получишь {tokens} жетонов."
        std::string details = Acore::StringFormat(
            "\u041f\u043e\u0441\u043b\u0443\u0448\u0430\u0439 \u043c\u0435\u043d\u044f, \u043f\u0443\u0442\u043d\u0438\u043a...\n\n"
            "\u0412 \u043d\u0430\u0448\u0438\u0445 \u043a\u0440\u0430\u044f\u0445 \u043b\u044e\u0442\u0443\u0435\u0442 {} "
            "(\u0440\u0430\u043d\u0433 {}).\n\n"
            "\u041f\u0440\u0438\u043d\u0435\u0441\u0438 \u0432\u0435\u0441\u0442\u044c \u043e \u0435\u0433\u043e "
            "\u0433\u0438\u0431\u0435\u043b\u0438 \u2014 \u0438 \u043f\u043e\u043b\u0443\u0447\u0438\u0448\u044c {} "
            "\u0436\u0435\u0442\u043e\u043d\u043e\u0432.",
            b.title, uint32(b.rank), tokens);

        // Use a unique textId per player so client cache doesn't collide.
        uint32 const customTextId = 0x7F000000u | (player->GetGUID().GetCounter() & 0x00FFFFFFu);
        SendCustomNpcText(player, details, customTextId);

        // "Принять контракт"
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
            "\u041f\u0440\u0438\u043d\u044f\u0442\u044c \u043a\u043e\u043d\u0442\u0440\u0430\u043a\u0442",
            GOSSIP_SENDER_MAIN, BOARD_ACCEPT_BASE_ACTION + slot);

        // "Назад"
        AddGossipItemFor(player, GOSSIP_ICON_CHAT,
            "\u041d\u0430\u0437\u0430\u0434",
            GOSSIP_SENDER_MAIN, BOARD_BACK_ACTION);

        SendGossipMenuFor(player, customTextId, creature->GetGUID());
    }

    // View of the player's currently active bounty, with abandon option.
    void ShowActiveBounty(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        NemesisBountyBoard::ActiveBounty active;
        if (!NemesisBountyBoard::GetActiveBounty(player, active))
        {
            ShowBoard(player, creature);
            return;
        }

        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const remaining = active.expiresAt > now ? active.expiresAt - now : 0u;
        uint32 const hoursLeft = remaining / 3600u;
        uint32 const minsLeft = (remaining % 3600u) / 60u;

        // Active-bounty flavor text.
        // "Я жду вестей о {title}.
        //  У тебя осталось {h} ч {m} мин. Не подведи."
        std::string details = Acore::StringFormat(
            "\u042f \u0436\u0434\u0443 \u0432\u0435\u0441\u0442\u0435\u0439 \u043e {}.\n\n"
            "\u0423 \u0442\u0435\u0431\u044f \u043e\u0441\u0442\u0430\u043b\u043e\u0441\u044c "
            "{} \u0447 {} \u043c\u0438\u043d. \u041d\u0435 \u043f\u043e\u0434\u0432\u0435\u0434\u0438.",
            active.targetTitle, hoursLeft, minsLeft);

        uint32 const customTextId = 0x7F000000u | (player->GetGUID().GetCounter() & 0x00FFFFFFu);
        SendCustomNpcText(player, details, customTextId);

        // "Отказаться"
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE,
            "\u041e\u0442\u043a\u0430\u0437\u0430\u0442\u044c\u0441\u044f",
            GOSSIP_SENDER_MAIN, BOARD_ABANDON_ACTION);

        // "Назад"
        AddGossipItemFor(player, GOSSIP_ICON_CHAT,
            "\u041d\u0430\u0437\u0430\u0434",
            GOSSIP_SENDER_MAIN, BOARD_BACK_ACTION);

        SendGossipMenuFor(player, customTextId, creature->GetGUID());
    }
};

void AddSC_mod_nemesis_system()
{
    new NemesisSystemPlayerScript();
    new NemesisSystemAllCreatureScript();
    new NemesisSystemUnitScript();
    new NemesisSystemCommandScript();
    new NemesisBountyVendorScript();
}