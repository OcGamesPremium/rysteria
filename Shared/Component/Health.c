// Copyright (C) 2024 Paul Johnson
// Copyright (C) 2024-2025 Maxim Nesterov

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <Shared/Component/Health.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <Shared/Entity.h>
#include <Shared/SimulationCommon.h>
#include <Shared/pb.h>

#define FOR_EACH_PUBLIC_FIELD                                                  \
    X(health, float32)                                                         \
    X(flags, uint8)                                                            \
    X(max_health, float32)

enum
{
    state_flags_max_health = 0b001,
    state_flags_flags = 0b010,
    state_flags_health = 0b100,
    state_flags_all = 0b111
};

void rr_component_health_init(struct rr_component_health *this,
                              struct rr_simulation *simulation)
{
    memset(this, 0, sizeof *this);
    this->health = 1;
    this->max_health = 1;
}

void rr_component_health_free(struct rr_component_health *this,
                              struct rr_simulation *simulation)
{
}

#ifdef RR_SERVER
#include <math.h>
#include <stdio.h>

#include <Server/Client.h>
#include <Server/EntityAllocation.h>
#include <Server/Simulation.h>

#include <Shared/Component/Petal.h>
#include <Shared/Component/PlayerInfo.h>
#include <Shared/Component/Relations.h>
#include <Shared/StaticData.h>
#include <Shared/Utilities.h>

#define RR_RUBY_SPAWN_LIFETIME (10 * 25)
#define RR_RUBY_SPAWN_MAX_MOBS 30

static EntityIdx rr_health_get_player_ruby_petal(
    struct rr_simulation *simulation,
    struct rr_component_player_info *player_info)
{
    for (uint32_t outer = 0; outer < player_info->slot_count; ++outer)
    {
        struct rr_component_player_info_petal_slot *slot =
            &player_info->slots[outer];
        for (uint32_t inner = 0; inner < 6; ++inner)
        {
            EntityHash hash = slot->petals[inner].entity_hash;
            if (hash == RR_NULL_ENTITY)
                continue;
            if (!rr_simulation_entity_alive(simulation, hash))
                continue;
            EntityIdx entity = (EntityIdx)hash;
            if (!rr_simulation_has_petal(simulation, entity))
                continue;
            struct rr_component_petal *petal =
                rr_simulation_get_petal(simulation, entity);
            if (petal->id == rr_petal_id_ruby)
                return entity;
        }
    }
    return RR_NULL_ENTITY;
}

static void rr_health_ruby_cleanup_spawned(struct rr_simulation *simulation,
                                           struct rr_component_petal *petal)
{
    for (uint8_t i = 0; i < RR_RUBY_SPAWN_MAX_MOBS; ++i)
    {
        EntityHash hash = petal->ruby_spawned_mobs[i];
        if (hash == RR_NULL_ENTITY)
            continue;
        if (!rr_simulation_entity_alive(simulation, hash))
        {
            petal->ruby_spawned_mobs[i] = RR_NULL_ENTITY;
            petal->ruby_spawned_lifespan[i] = 0;
            if (petal->ruby_spawned_count > 0)
                --petal->ruby_spawned_count;
            continue;
        }
        if (petal->ruby_spawned_lifespan[i] > 0 &&
            --petal->ruby_spawned_lifespan[i] == 0)
        {
            rr_simulation_request_entity_deletion(simulation, (EntityIdx)hash);
            petal->ruby_spawned_mobs[i] = RR_NULL_ENTITY;
            if (petal->ruby_spawned_count > 0)
                --petal->ruby_spawned_count;
        }
    }
}

static void rr_health_spawn_ruby_mob(struct rr_simulation *simulation,
                                     struct rr_component_player_info *player_info,
                                     EntityIdx target)
{
    EntityIdx ruby_petal = rr_health_get_player_ruby_petal(simulation, player_info);
    if (ruby_petal == RR_NULL_ENTITY)
        return;

    struct rr_component_petal *petal =
        rr_simulation_get_petal(simulation, ruby_petal);
    rr_health_ruby_cleanup_spawned(simulation, petal);
    if (petal->ruby_spawned_count >= RR_RUBY_SPAWN_MAX_MOBS)
        return;

    struct rr_component_mob *target_mob = rr_simulation_get_mob(simulation, target);
    if (target_mob == NULL)
        return;

    uint8_t spawn_rarity = target_mob->rarity > 0 ? target_mob->rarity - 1 : 0;

    struct rr_component_physical *target_physical =
        rr_simulation_get_physical(simulation, target);
    struct rr_component_relations *owner_relations =
        rr_simulation_get_relations(simulation, player_info->flower_id);
    EntityIdx mob_id = rr_simulation_alloc_mob(
        simulation, target_physical->arena, target_physical->x,
        target_physical->y, target_mob->id, spawn_rarity,
        owner_relations->team);
    struct rr_component_relations *mob_relations =
        rr_simulation_get_relations(simulation, mob_id);
    rr_component_relations_set_owner(mob_relations, player_info->flower_id);
    rr_component_relations_update_root_owner(simulation, mob_relations);
    /* spawned by ruby: do not mark as player_spawned so they despawn
       according to ticks_to_despawn. */
    rr_simulation_get_mob(simulation, mob_id)->ticks_to_despawn =
        RR_RUBY_SPAWN_LIFETIME;

    for (uint8_t i = 0; i < RR_RUBY_SPAWN_MAX_MOBS; ++i)
    {
        if (petal->ruby_spawned_mobs[i] == RR_NULL_ENTITY)
        {
            petal->ruby_spawned_mobs[i] = rr_simulation_get_entity_hash(simulation, mob_id);
            petal->ruby_spawned_lifespan[i] = RR_RUBY_SPAWN_LIFETIME;
            ++petal->ruby_spawned_count;
            break;
        }
    }
}

void rr_component_health_write(struct rr_component_health *this,
                               struct proto_bug *encoder, int is_creation,
                               struct rr_component_player_info *client)
{
    uint64_t state = this->protocol_state | (state_flags_all * is_creation);
    proto_bug_write_varuint(encoder, state, "health component state");
    float tmp_health = this->health;
    float tmp_max = this->max_health;
    if (this->flags & 1)
    {
        this->health = this->max_health = 0;
    }
#define X(NAME, TYPE) RR_ENCODE_PUBLIC_FIELD(NAME, TYPE);
    FOR_EACH_PUBLIC_FIELD
#undef X
    this->health = tmp_health;
    this->max_health = tmp_max;
}

void rr_component_health_do_damage(struct rr_simulation *simulation,
                                   struct rr_component_health *this,
                                   EntityIdx from, float v, uint8_t color_type)
{
    uint8_t no_reduction = this->flags & 4;
    this->flags &= ~4;
    if (dev_cheat_enabled(simulation, this->parent_id, invulnerable))
        return;
    if (this->health == 0)
        return;
    if (v == 0)
        return;
    /*if (rr_simulation_has_mob(simulation, from))
    {
        struct rr_component_mob *from_mob = rr_simulation_get_mob(simulation, from);
        if (from_mob->id == rr_mob_id_edmontosaurus)
            no_reduction = 1; // blame Lans ST
        if (rr_simulation_has_mob(simulation, this->parent_id))
        {
            struct rr_component_mob *mob = rr_simulation_get_mob(simulation, this->parent_id);
            if (mob->id == rr_mob_id_edmontosaurus && from_mob->player_spawned &&
                (from_mob->rarity < mob->rarity || from_mob->rarity - mob->rarity < 2))
                return; // blame Lans ST
        }
    }*/
    if (!no_reduction)
    {
        v *= 1 - this->damage_reduction_ratio;
        v -= this->damage_reduction;
    }
    if (v < 0)
        v = 0;
    v = this->health - v;
    if (v < 0)
        v = 0;
    float damage = this->health - v;
    rr_component_health_set_health(this, v);
    EntityHash p_info_id =
        rr_simulation_get_relations(simulation, from)->root_owner;
    if (!rr_simulation_entity_alive(simulation, p_info_id))
        return;
    if (!rr_simulation_has_player_info(simulation, p_info_id))
        return;
    if (!rr_simulation_has_mob(simulation, this->parent_id) &&
        !rr_simulation_has_flower(simulation, this->parent_id))
        return;
    if (rr_simulation_has_mob(simulation, this->parent_id) &&
        rr_simulation_get_mob(simulation, this->parent_id)->player_spawned)
        return;
    if (is_same_team(
            rr_simulation_get_relations(simulation, this->parent_id)->team,
            rr_simulation_get_relations(simulation, from)->team))
        return;
    if (rr_simulation_has_petal(simulation, from))
    {
        struct rr_component_petal *petal =
            rr_simulation_get_petal(simulation, from);
        if (petal->id == rr_petal_id_beak)
        {
            struct rr_component_physical *physical =
                rr_simulation_get_physical(simulation, this->parent_id);
            float duration = 25.0f *
                             (petal->rarity == rr_rarity_id_common
                                  ? 1.0f
                                  : petal->rarity == rr_rarity_id_unusual
                                        ? 1.2f
                                        : petal->rarity == rr_rarity_id_rare
                                              ? 1.38f
                                              : petal->rarity == rr_rarity_id_epic
                                                    ? 1.62f
                                                    : petal->rarity == rr_rarity_id_legendary
                                                          ? 1.91f
                                                          : petal->rarity == rr_rarity_id_mythic
                                                                ? 2.24f
                                                                : petal->rarity == rr_rarity_id_exotic
                                                                      ? 2.63f
                                                                      : petal->rarity == rr_rarity_id_ultimate
                                                                            ? 3.09f
                                                                            : petal->rarity == rr_rarity_id_quantum
                                                                                  ? 3.63f
                                                                                  : petal->rarity == rr_rarity_id_aurous
                                                                                        ? 4.27f
                                                                                        : petal->rarity == rr_rarity_id_eternal
                                                                                              ? 5.01f
                                                                                              : petal->rarity == rr_rarity_id_hyper
                                                                                                    ? 5.89f
                                                                                                    : petal->rarity == rr_rarity_id_sunshine
                                                                                                          ? 6.93f
                                                                                                          : petal->rarity == rr_rarity_id_nebula
                                                                                                                ? 8.13f
                                                                                                                : petal->rarity == rr_rarity_id_infinity
                                                                                                                      ? 9.56f
                                                                                                                      : petal->rarity == rr_rarity_id_calamity
                                                                                                                            ? 11.23f
                                                                                                                            : petal->rarity == rr_rarity_id_unique
                                                                                                                                  ? 13.2f
                                                                                                                                  : petal->rarity == rr_rarity_id_cosmic
                                                                                                                                        ? 15.51f
                                                                                                                                        : petal->rarity == rr_rarity_id_galactic
                                                                                                                                              ? 18.23f
                                                                                                                                              : 20.0f);
            physical->stun_ticks = (uint32_t)fmaxf((float)physical->stun_ticks,
                                                   duration);
        }
        else if (petal->id == rr_petal_id_sapphire)
        {
            struct rr_component_physical *physical =
                rr_simulation_get_physical(simulation, this->parent_id);
            physical->stun_ticks = ~0u;
        }
    }
    struct rr_component_player_info *player_info =
        rr_simulation_get_player_info(simulation, p_info_id);
    this->squad_damage_counter[player_info->squad] += damage;
    struct rr_component_physical *physical =
        rr_simulation_get_physical(simulation, this->parent_id);
    if (v == 0 && rr_simulation_has_mob(simulation, this->parent_id))
    {
        if (rr_simulation_has_petal(simulation, from) &&
            rr_simulation_get_petal(simulation, from)->id == rr_petal_id_emerald)
        {
            struct rr_component_petal *petal =
                rr_simulation_get_petal(simulation, from);
            struct rr_component_health *petal_health =
                rr_simulation_get_health(simulation, from);
            struct rr_component_mob *mob =
                rr_simulation_get_mob(simulation, this->parent_id);
            float bonus_percent = 1.0f + 0.25f * mob->rarity;
            petal->emerald_bonus_percent += bonus_percent;
            if (petal->emerald_bonus_percent > 400.0f)
                petal->emerald_bonus_percent = 400.0f;
            if (petal->p_petal != NULL)
                petal->p_petal->emerald_bonus_percent =
                    petal->emerald_bonus_percent;
            if (petal->slot != NULL && player_info != NULL)
            {
                petal->slot->client_emerald_bonus = petal->emerald_bonus_percent;
                // Find slot position and mark protocol state
                for (uint8_t i = 0; i < player_info->slot_count; ++i)
                {
                    if (&player_info->slots[i] == petal->slot)
                    {
                        player_info->protocol_state |= 1; // state_flags_petals
                        break;
                    }
                }
            }

            float base_damage = RR_PETAL_DATA[petal->id].damage *
                                RR_PETAL_DATA[petal->id].scale[petal->rarity].damage /
                                RR_PETAL_DATA[petal->id].count[petal->rarity];
            float multiplier = 1.0f + petal->emerald_bonus_percent / 100.0f;
            petal_health->damage = base_damage * multiplier;
        }
        rr_health_spawn_ruby_mob(simulation, player_info, this->parent_id);
    }
    struct rr_simulation_animation *animation =
        &simulation->animations[simulation->animation_length++];
    animation->type = rr_animation_type_damagenumber;
    animation->owner = from;
    animation->x = physical->x;
    animation->y = physical->y;
    animation->damage = ceilf(damage);
    animation->squad = player_info->squad;
    animation->color_type = color_type;
}

void rr_component_health_set_health(struct rr_component_health *this, float v)
{
    if (this->health == 0)
        return;
    if (v > this->max_health)
        v = this->max_health;
    else if (v < 0)
        v = 0;
    if (v < this->health)
        rr_component_health_set_flags(this, this->flags | 2);
    this->protocol_state |= (v != this->health) * state_flags_health;
    this->health = v;
}

RR_DEFINE_PUBLIC_FIELD(health, float, max_health)
RR_DEFINE_PUBLIC_FIELD(health, uint8_t, flags)

#endif

#ifdef RR_CLIENT
void rr_component_health_read(struct rr_component_health *this,
                              struct proto_bug *encoder)
{
    uint64_t state = proto_bug_read_varuint(encoder, "health component state");
#define X(NAME, TYPE) RR_DECODE_PUBLIC_FIELD(NAME, TYPE);
    FOR_EACH_PUBLIC_FIELD
#undef X
}
#endif
