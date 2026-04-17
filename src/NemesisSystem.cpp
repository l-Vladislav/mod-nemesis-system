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
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Mail.h"
#include "Player.h"
#include "Random.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "UnitScript.h"
#include "WorldPacket.h"
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
#include <sstream>
#include <vector>
#include <unordered_map>

using namespace Acore::ChatCommands;

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

    std::string GenerateNemesisTitle(ObjectGuid::LowType spawnId, uint32 creatureEntry)
    {
        uint32 const seed = uint32(spawnId) ^ (creatureEntry * 2654435761u);
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

        return view;
    }

    std::string BuildAddonEntryPayload(char const* opcode, NemesisAddonView const& view)
    {
        return Acore::StringFormat(
            "V2:{}:{}:{}:{}:{}:{}:{}:{:.1f}:{:.1f}:{:.1f}:{:.2f}:{:.2f}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
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
            view.localizedName);
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

    void SaveNemesisState(Creature* creature, NemesisState const& state)
    {
        if (!creature)
            return;

        if (!creature->GetSpawnId())
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
            uint64(creature->GetSpawnId()),
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

        ActiveNemeses[creature->GetSpawnId()] = storedState;
    }

    void DeleteNemesisState(ObjectGuid::LowType spawnId, char const* reason = "cleared")
    {
        if (!spawnId)
            return;

        EnsureCacheLoaded();

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
        NemesisState state;
        bool const existed = TryGetNemesisState(killer, state);
        uint8 const previousRank = state.rank;
        uint32 const now = uint32(GameTime::GetGameTime().count());

        if (existed)
        {
            if (state.rank < GetMaxRank())
                ++state.rank;
        }
        else
            state = BuildInitialNemesisState(killer, killed);

        state.creatureEntry = killer->GetEntry();
        state.mapId = killer->GetMapId();
        state.zoneId = killer->GetZoneId();
        state.targetGuid = killed->GetGUID().GetCounter();
        state.lastPromotionAt = now;
        state.lastVictimGuid = killed->GetGUID().GetCounter();
        state.createdAt = now;
        state.lastSeenAt = now;
        RollAffixes(state);

        SaveNemesisState(killer, state);
        ApplyNemesisState(killer, state);
        killer->SetFullHealth();
        BroadcastRankFiveNemesisIfPersistent(killer, state);

        // Push nemesis data to the victim (the player who was killed)
        if (killer->GetSpawnId() && killed)
        {
            NemesisAddonView const view = BuildAddonView(killed, killer->GetSpawnId(), state);
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

class NemesisSystemPlayerScript : public PlayerScript
{
public:
    NemesisSystemPlayerScript() : PlayerScript("NemesisSystemPlayerScript", { PLAYERHOOK_ON_PLAYER_KILLED_BY_CREATURE, PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_CREATURE_KILLED_BY_PET }) { }

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

class NemesisSystemAllCreatureScript : public AllCreatureScript
{
public:
    NemesisSystemAllCreatureScript() : AllCreatureScript("NemesisSystemAllCreatureScript") { }

    void OnCreatureAddWorld(Creature* creature) override
    {
        NemesisState state;
        if (!TryGetNemesisState(creature, state))
            return;

        ApplyNemesisState(creature, state);
        state.zoneId = creature->GetZoneId();
        state.homeX = creature->GetPositionX();
        state.homeY = creature->GetPositionY();
        state.homeZ = creature->GetPositionZ();
        state.lastSeenAt = uint32(GameTime::GetGameTime().count());

        if (ObjectGuid::LowType const spawnId = creature->GetSpawnId())
            ActiveNemeses[spawnId] = state;
        else if (IsTemporaryNemesisCandidate(creature))
            ActiveTemporaryNemeses[creature->GetGUID()] = state;
    }

    void OnCreatureRemoveWorld(Creature* creature) override
    {
        if (!creature || creature->GetSpawnId())
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
            { "reload", HandleReload, SEC_ADMINISTRATOR, Console::Yes }
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

    static bool HandleAddonReport(ChatHandler* handler, uint64 rawSpawnId)
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

        uint32 const now = uint32(GameTime::GetGameTime().count());
        if (state.lastSeenAt && state.lastSeenAt + GetAddonReportCooldownSeconds() > now)
            return true;

        state.mapId = player->GetMapId();
        state.zoneId = player->GetZoneId();
        state.homeX = player->GetPositionX();
        state.homeY = player->GetPositionY();
        state.homeZ = player->GetPositionZ();
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
        ActiveNemeses.clear();
        ActiveTemporaryNemeses.clear();
        RegenTickAccumulators.clear();
        TemporaryRegenTickAccumulators.clear();
        CharacterDatabase.Execute("DELETE FROM `character_nemesis`");
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
};

// ────────────────────────────────────────────────────────────────────────────
// Bounty Vendor — adds "Bounty Hunter Rewards" gossip option to all innkeepers
// ────────────────────────────────────────────────────────────────────────────

class NemesisBountyVendorScript : public AllCreatureScript
{
    // Innkeeper gossip constants (mirrors npc_innkeeper.cpp)
    static constexpr uint32 INNKEEPER_GOSSIP_MENU    = 9733;
    static constexpr uint32 INNKEEPER_GOSSIP_EVENT   = 342;
    static constexpr uint32 SPELL_TRICK              = 24714;
    static constexpr uint32 SPELL_TREAT              = 24715;
    static constexpr uint32 SPELL_TRICKED_OR_TREATED = 24755;
    static constexpr uint32 HALLOWEEN_EVENTID        = 12;

    // Our custom action
    static constexpr uint32 BOUNTY_GOSSIP_ACTION = GOSSIP_ACTION_INFO_DEF + 9000;

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

        // ── Bounty vendor ──
        if (action == BOUNTY_GOSSIP_ACTION)
        {
            uint32 vendorEntry = sConfigMgr->GetOption<uint32>(
                "NemesisSystem.BountyVendor.Entry", 190000);

            // Temporarily grant vendor flag so SendListInventory succeeds
            bool hadVendorFlag = creature->HasNpcFlag(UNIT_NPC_FLAG_VENDOR);
            if (!hadVendorFlag)
                creature->SetNpcFlag(UNIT_NPC_FLAG_VENDOR);

            player->GetSession()->SendListInventory(creature->GetGUID(), vendorEntry);

            if (!hadVendorFlag)
                creature->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);

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
};

void AddSC_mod_nemesis_system()
{
    new NemesisSystemPlayerScript();
    new NemesisSystemAllCreatureScript();
    new NemesisSystemUnitScript();
    new NemesisSystemCommandScript();
    new NemesisBountyVendorScript();
}