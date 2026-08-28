/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Portable extras derived from Hunter Davis's Johnny Castaway PS1 port:
 * https://github.com/huntergdavis/johnny-castaway-ps1
 *
 * The holiday rows are a compact translation of holidays.yml and the
 * generated src/scene/holidays_table.c at public commit 25c5d8459. Date
 * calculations follow src/scene/holidays.c. The Gregorian Easter formula is
 * the public Meeus/Jones/Butcher algorithm, as credited by the PS1 project.
 *
 * The ambience metadata describes OCEAN.VAG introduced by Hunter Davis in
 * commit bb32de68a. The recording is BigSoundBank sound 0266, "Sea: Waves",
 * CC0/public domain. This module does not embed or redistribute that binary.
 */
#include "jc_extras.h"

#include <string.h>

#define HOLIDAY(id_, title_, short_, label_, kind_, month_, day_, nth_,     \
                weekday_, offset_, sprite_, original_)                    \
    { (id_), (title_), (short_), (label_), (kind_), (month_), (day_),      \
      (nth_), (weekday_), (offset_), (sprite_), (original_) }

static const jc_holiday_extra_t holidays[] = {
    HOLIDAY(4, "New Year's Day", "NEW YEAR", "JAN 1",
            JC_HOLIDAY_RULE_FIXED, 1, 1, 0, 0, 0, 3, true),
    HOLIDAY(5, "Elvis's Birthday", "ELVIS BDAY", "JAN 8",
            JC_HOLIDAY_RULE_FIXED, 1, 8, 0, 0, 0, 4, false),
    HOLIDAY(6, "MLK Jr. Day", "MLK DAY", "3RD MON JAN",
            JC_HOLIDAY_RULE_NTH_WEEKDAY, 1, 0, 3, 1, 0, 5, false),
    HOLIDAY(7, "Groundhog Day", "GROUNDHOG", "FEB 2",
            JC_HOLIDAY_RULE_FIXED, 2, 2, 0, 0, 0, 6, false),
    HOLIDAY(8, "Valentine's Day", "VALENTINE", "FEB 14",
            JC_HOLIDAY_RULE_FIXED, 2, 14, 0, 0, 0, 7, false),
    HOLIDAY(9, "Super Bowl Sunday", "SUPER BOWL", "2ND SUN FEB",
            JC_HOLIDAY_RULE_NTH_WEEKDAY, 2, 0, 2, 0, 0, 8, false),
    HOLIDAY(10, "Presidents' Day", "PRESIDENTS", "3RD MON FEB",
            JC_HOLIDAY_RULE_NTH_WEEKDAY, 2, 0, 3, 1, 0, 9, false),
    HOLIDAY(11, "Mardi Gras", "MARDI GRAS", "EASTER-47",
            JC_HOLIDAY_RULE_EASTER_OFFSET, 0, 0, 0, 0, -47, 10, false),
    HOLIDAY(12, "Pi Day", "PI DAY", "MAR 14",
            JC_HOLIDAY_RULE_FIXED, 3, 14, 0, 0, 0, 11, false),
    HOLIDAY(2, "St. Patrick's Day", "ST PATRICK", "MAR 17",
            JC_HOLIDAY_RULE_FIXED, 3, 17, 0, 0, 0, 1, true),
    HOLIDAY(13, "First Day of Spring", "SPRING", "MAR 20",
            JC_HOLIDAY_RULE_VERNAL_EQUINOX, 3, 0, 0, 0, 0, 12, false),
    HOLIDAY(14, "April Fool's Day", "APRIL FOOL", "APR 1",
            JC_HOLIDAY_RULE_FIXED, 4, 1, 0, 0, 0, 13, false),
    HOLIDAY(36, "4/20 Day", "420 DAY", "APR 20",
            JC_HOLIDAY_RULE_FIXED, 4, 20, 0, 0, 0, 14, false),
    HOLIDAY(15, "Easter", "EASTER", "EASTER",
            JC_HOLIDAY_RULE_EASTER_OFFSET, 0, 0, 0, 0, 0, 15, false),
    HOLIDAY(16, "Earth Day", "EARTH DAY", "APR 22",
            JC_HOLIDAY_RULE_FIXED, 4, 22, 0, 0, 0, 16, false),
    HOLIDAY(17, "Star Wars Day", "STAR WARS", "MAY 4",
            JC_HOLIDAY_RULE_FIXED, 5, 4, 0, 0, 0, 17, false),
    HOLIDAY(18, "Cinco de Mayo", "CINCO MAYO", "MAY 5",
            JC_HOLIDAY_RULE_FIXED, 5, 5, 0, 0, 0, 18, false),
    HOLIDAY(19, "Mother's Day", "MOTHERS DAY", "2ND SUN MAY",
            JC_HOLIDAY_RULE_NTH_WEEKDAY, 5, 0, 2, 0, 0, 19, false),
    HOLIDAY(20, "Memorial Day", "MEMORIAL", "LAST MON MAY",
            JC_HOLIDAY_RULE_NTH_WEEKDAY, 5, 0, -1, 1, 0, 20, false),
    HOLIDAY(21, "Father's Day", "FATHERS DAY", "3RD SUN JUN",
            JC_HOLIDAY_RULE_NTH_WEEKDAY, 6, 0, 3, 0, 0, 21, false),
    HOLIDAY(22, "First Day of Summer", "SUMMER", "JUN 21",
            JC_HOLIDAY_RULE_SUMMER_SOLSTICE, 6, 0, 0, 0, 0, 22, false),
    HOLIDAY(23, "Pride Day", "PRIDE", "LAST SUN JUN",
            JC_HOLIDAY_RULE_NTH_WEEKDAY, 6, 0, -1, 0, 0, 23, false),
    HOLIDAY(24, "Independence Day", "JULY 4TH", "JUL 4",
            JC_HOLIDAY_RULE_FIXED, 7, 4, 0, 0, 0, 24, false),
    HOLIDAY(25, "Moon Landing Day", "MOON LAND", "JUL 20",
            JC_HOLIDAY_RULE_FIXED, 7, 20, 0, 0, 0, 25, false),
    HOLIDAY(26, "National Watermelon Day", "WATERMELON", "AUG 3",
            JC_HOLIDAY_RULE_FIXED, 8, 3, 0, 0, 0, 26, false),
    HOLIDAY(27, "Left-Handers Day", "LEFT HAND", "AUG 13",
            JC_HOLIDAY_RULE_FIXED, 8, 13, 0, 0, 0, 27, false),
    HOLIDAY(28, "Hawaii Statehood Day", "HAWAII DAY", "AUG 21",
            JC_HOLIDAY_RULE_FIXED, 8, 21, 0, 0, 0, 28, false),
    HOLIDAY(29, "Labor Day", "LABOR DAY", "1ST MON SEP",
            JC_HOLIDAY_RULE_NTH_WEEKDAY, 9, 0, 1, 1, 0, 29, false),
    HOLIDAY(30, "Talk Like a Pirate Day", "PIRATE DAY", "SEP 19",
            JC_HOLIDAY_RULE_FIXED, 9, 19, 0, 0, 0, 30, false),
    HOLIDAY(31, "First Day of Autumn", "AUTUMN", "SEP 22",
            JC_HOLIDAY_RULE_AUTUMNAL_EQUINOX, 9, 0, 0, 0, 0, 31, false),
    HOLIDAY(32, "Columbus / Indigenous Peoples' Day", "COLUMBUS",
            "2ND MON OCT", JC_HOLIDAY_RULE_NTH_WEEKDAY,
            10, 0, 2, 1, 0, 32, false),
    HOLIDAY(1, "Halloween", "HALLOWEEN", "OCT 31",
            JC_HOLIDAY_RULE_FIXED, 10, 31, 0, 0, 0, 0, true),
    HOLIDAY(33, "Election Day", "ELECTION", "NOV 2-8",
            JC_HOLIDAY_RULE_ELECTION_DAY, 11, 0, 0, 0, 0, 33, false),
    HOLIDAY(34, "Veterans Day", "VETERANS", "NOV 11",
            JC_HOLIDAY_RULE_FIXED, 11, 11, 0, 0, 0, 34, false),
    HOLIDAY(35, "Thanksgiving", "THANKSGIVE", "4TH THU NOV",
            JC_HOLIDAY_RULE_NTH_WEEKDAY, 11, 0, 4, 4, 0, 35, false),
    HOLIDAY(3, "Christmas", "CHRISTMAS", "DEC 25",
            JC_HOLIDAY_RULE_FIXED, 12, 25, 0, 0, 0, 2, true)
};

static const jc_ambience_asset_t ocean_asset = {
    "ocean",
    "Ocean waves",
    "OCEAN.VAG",
    "https://bigsoundbank.com/sea-waves-s0266.html",
    "CC0 1.0 / public domain",
    "b9eeae5a7f42545ad7fe99701c248c07e8b4c0ad0ab17bb86420f36ea97259c2",
    11025u,
    20000u,
    1u,
    true
};

static bool leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month)
{
    static const uint8_t days[] = {
        0u, 31u, 28u, 31u, 30u, 31u, 30u,
        31u, 31u, 30u, 31u, 30u, 31u
    };
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && leap_year(year))
        return 29;
    return days[month];
}

int jc_holiday_day_of_week(int year, int month, int day)
{
    int century;
    int year_of_century;
    int result;
    if (year < 1583 || year > 4099 || month < 1 || month > 12 ||
        day < 1 || day > days_in_month(year, month))
        return -1;
    if (month < 3) {
        month += 12;
        --year;
    }
    year_of_century = year % 100;
    century = year / 100;
    result = (day + 13 * (month + 1) / 5 + year_of_century +
              year_of_century / 4 + century / 4 + 5 * century) % 7;
    return (result + 6) % 7;
}

static int nth_weekday(int year, int month, int nth, int weekday)
{
    int first;
    int result;
    int limit;
    first = jc_holiday_day_of_week(year, month, 1);
    if (first < 0 || weekday < 0 || weekday > 6)
        return 0;
    result = 1 + (weekday - first + 7) % 7;
    limit = days_in_month(year, month);
    if (nth == -1) {
        while (result + 7 <= limit)
            result += 7;
        return result;
    }
    if (nth < 1 || nth > 5)
        return 0;
    result += (nth - 1) * 7;
    return result <= limit ? result : 0;
}

void jc_holiday_easter_sunday(int year, int *month, int *day)
{
    int a;
    int b;
    int c;
    int d;
    int e;
    int f;
    int g;
    int h;
    int i;
    int k;
    int l;
    int m;
    int value;
    if (year < 1583 || year > 4099) {
        if (month != NULL)
            *month = 0;
        if (day != NULL)
            *day = 0;
        return;
    }
    a = year % 19;
    b = year / 100;
    c = year % 100;
    d = b / 4;
    e = b % 4;
    f = (b + 8) / 25;
    g = (b - f + 1) / 3;
    h = (19 * a + b - d - g + 15) % 30;
    i = c / 4;
    k = c % 4;
    l = (32 + 2 * e + 2 * i - h - k) % 7;
    m = (a + 11 * h + 22 * l) / 451;
    value = h + l - 7 * m + 114;
    if (month != NULL)
        *month = value / 31;
    if (day != NULL)
        *day = value % 31 + 1;
}

static void add_days(int *year, int *month, int *day, int delta)
{
    *day += delta;
    while (*day < 1) {
        --*month;
        if (*month < 1) {
            *month = 12;
            --*year;
        }
        *day += days_in_month(*year, *month);
    }
    while (*day > days_in_month(*year, *month)) {
        *day -= days_in_month(*year, *month);
        ++*month;
        if (*month > 12) {
            *month = 1;
            ++*year;
        }
    }
}

size_t jc_holiday_extra_count(void)
{
    return sizeof(holidays) / sizeof(holidays[0]);
}

const jc_holiday_extra_t *jc_holiday_extra_at(size_t index)
{
    return index < jc_holiday_extra_count() ? &holidays[index] : NULL;
}

const jc_holiday_extra_t *jc_holiday_extra_by_id(int id)
{
    size_t index;
    for (index = 0u; index < jc_holiday_extra_count(); ++index) {
        if (holidays[index].id == id)
            return &holidays[index];
    }
    return NULL;
}

const jc_holiday_extra_t *jc_holiday_extra_for_date(int year, int month,
                                                    int day,
                                                    bool original_four_only)
{
    size_t index;
    if (year < 1583 || year > 4099 || month < 1 || month > 12 ||
        day < 1 || day > days_in_month(year, month))
        return NULL;
    for (index = 0u; index < jc_holiday_extra_count(); ++index) {
        const jc_holiday_extra_t *holiday = &holidays[index];
        int rule_year = year;
        int rule_month = holiday->month;
        int rule_day = holiday->day;
        if (original_four_only && !holiday->original_four)
            continue;
        switch (holiday->kind) {
        case JC_HOLIDAY_RULE_FIXED:
            break;
        case JC_HOLIDAY_RULE_NTH_WEEKDAY:
            rule_day = nth_weekday(year, holiday->month, holiday->nth,
                                   holiday->weekday);
            break;
        case JC_HOLIDAY_RULE_EASTER_OFFSET:
            jc_holiday_easter_sunday(year, &rule_month, &rule_day);
            add_days(&rule_year, &rule_month, &rule_day,
                     holiday->easter_offset);
            break;
        case JC_HOLIDAY_RULE_WINTER_SOLSTICE:
            rule_month = 12;
            rule_day = 21;
            break;
        case JC_HOLIDAY_RULE_SUMMER_SOLSTICE:
            rule_month = 6;
            rule_day = 21;
            break;
        case JC_HOLIDAY_RULE_VERNAL_EQUINOX:
            rule_month = 3;
            rule_day = 20;
            break;
        case JC_HOLIDAY_RULE_AUTUMNAL_EQUINOX:
            rule_month = 9;
            rule_day = 22;
            break;
        case JC_HOLIDAY_RULE_ELECTION_DAY:
            rule_month = 11;
            rule_day = nth_weekday(year, 11, 1, 1) + 1;
            break;
        default:
            continue;
        }
        if (rule_year == year && rule_month == month && rule_day == day)
            return holiday;
    }
    return NULL;
}

const jc_ambience_asset_t *jc_ambience_ocean_asset(void)
{
    return &ocean_asset;
}

void jc_ambience_config_init(jc_ambience_config_t *config)
{
    if (config == NULL)
        return;
    config->enabled = true;
    config->volume = 56u;
}

void jc_ambience_config_set_enabled(jc_ambience_config_t *config,
                                    bool enabled)
{
    if (config != NULL)
        config->enabled = enabled;
}

void jc_ambience_config_set_volume(jc_ambience_config_t *config,
                                   unsigned volume)
{
    if (config == NULL)
        return;
    if (volume > 100u)
        volume = 100u;
    config->volume = (uint8_t)volume;
}

