#include "g_local.h"
#define HEALTH_IGNORE_MAX	1

/*
=================
check_dodge

This is a support routine used when a client is firing
a non-instant attack weapon.  It checks to see if a
monster's dodge function should be called.
=================
*/
static void P_ProjectSource (gclient_t *client, vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
	vec3_t	_distance;

	VectorCopy (distance, _distance);
	if (client->pers.hand == LEFT_HANDED)
		_distance[1] *= -1;
	else if (client->pers.hand == CENTER_HANDED)
		_distance[1] = 0;
	G_ProjectSource (point, _distance, forward, right, result);
}
static void check_dodge (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	vec3_t	end;
	vec3_t	v;
	trace_t	tr;
	float	eta;

	// easy mode only ducks one quarter the time
	if (skill->value == 0)
	{
		if (random() > 0.25)
			return;
	}
	VectorMA (start, 8192, dir, end);
	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);
	if ((tr.ent) && (tr.ent->svflags & SVF_MONSTER) && (tr.ent->health > 0) && (tr.ent->monsterinfo.dodge) && infront(tr.ent, self))
	{
		VectorSubtract (tr.endpos, start, v);
		eta = (VectorLength(v) - tr.ent->maxs[0]) / speed;
		tr.ent->monsterinfo.dodge (tr.ent, self, eta);
	}
}


/*
=================
fire_hit

Used for all impact (hit/punch/slash) attacks
=================
*/
qboolean fire_hit (edict_t *self, vec3_t aim, int damage, int kick)
{
	trace_t		tr;
	vec3_t		forward, right, up;
	vec3_t		v;
	vec3_t		point;
	float		range;
	vec3_t		dir;

	//see if enemy is in range
	VectorSubtract (self->enemy->s.origin, self->s.origin, dir);
	range = VectorLength(dir);
	if (range > aim[0])
		return false;

	if (aim[1] > self->mins[0] && aim[1] < self->maxs[0])
	{
		// the hit is straight on so back the range up to the edge of their bbox
		range -= self->enemy->maxs[0];
	}
	else
	{
		// this is a side hit so adjust the "right" value out to the edge of their bbox
		if (aim[1] < 0)
			aim[1] = self->enemy->mins[0];
		else
			aim[1] = self->enemy->maxs[0];
	}

	VectorMA (self->s.origin, range, dir, point);

	tr = gi.trace (self->s.origin, NULL, NULL, point, self, MASK_SHOT);
	if (tr.fraction < 1)
	{
		if (!tr.ent->takedamage)
			return false;
		// if it will hit any client/monster then hit the one we wanted to hit
		if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
			tr.ent = self->enemy;
	}

	AngleVectors(self->s.angles, forward, right, up);
	VectorMA (self->s.origin, range, forward, point);
	VectorMA (point, aim[1], right, point);
	VectorMA (point, aim[2], up, point);
	VectorSubtract (point, self->enemy->s.origin, dir);

	// do the damage
	T_Damage (tr.ent, self, self, dir, point, vec3_origin, damage, kick/2, DAMAGE_NO_KNOCKBACK, MOD_HIT);

	if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
		return false;

	// do our special form of knockback here
	VectorMA (self->enemy->absmin, 0.5, self->enemy->size, v);
	VectorSubtract (v, point, v);
	VectorNormalize (v);
	VectorMA (self->enemy->velocity, kick, v, self->enemy->velocity);
	if (self->enemy->velocity[2] > 0)
		self->enemy->groundentity = NULL;
	return true;
}


/*
=================
fire_lead

This is an internal support routine used for bullet/pellet based weapons.
=================
*/
static void fire_lead (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int te_impact, int hspread, int vspread, int mod)
{
	trace_t		tr;
	vec3_t		dir;
	vec3_t		forward, right, up;
	vec3_t		end;
	float		r;
	float		u;
	vec3_t		water_start;
	qboolean	water = false;
	int			content_mask = MASK_SHOT | MASK_WATER;

	tr = gi.trace (self->s.origin, NULL, NULL, start, self, MASK_SHOT);
	if (!(tr.fraction < 1.0))
	{
		vectoangles (aimdir, dir);
		AngleVectors (dir, forward, right, up);

		r = crandom()*hspread;
		u = crandom()*vspread;
		VectorMA (start, 8192, forward, end);
		VectorMA (end, r, right, end);
		VectorMA (end, u, up, end);

		if (gi.pointcontents (start) & MASK_WATER)
		{
			water = true;
			VectorCopy (start, water_start);
			content_mask &= ~MASK_WATER;
		}

		tr = gi.trace (start, NULL, NULL, end, self, content_mask);

		// see if we hit water
		if (tr.contents & MASK_WATER)
		{
			int		color;

			water = true;
			VectorCopy (tr.endpos, water_start);

			if (!VectorCompare (start, tr.endpos))
			{
				if (tr.contents & CONTENTS_WATER)
				{
					if (strcmp(tr.surface->name, "*brwater") == 0)
						color = SPLASH_BROWN_WATER;
					else
						color = SPLASH_BLUE_WATER;
				}
				else if (tr.contents & CONTENTS_SLIME)
					color = SPLASH_SLIME;
				else if (tr.contents & CONTENTS_LAVA)
					color = SPLASH_LAVA;
				else
					color = SPLASH_UNKNOWN;

				if (color != SPLASH_UNKNOWN)
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (TE_SPLASH);
					gi.WriteByte (8);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.WriteByte (color);
					gi.multicast (tr.endpos, MULTICAST_PVS);
				}

				// change bullet's course when it enters water
				VectorSubtract (end, start, dir);
				vectoangles (dir, dir);
				AngleVectors (dir, forward, right, up);
				r = crandom()*hspread*2;
				u = crandom()*vspread*2;
				VectorMA (water_start, 8192, forward, end);
				VectorMA (end, r, right, end);
				VectorMA (end, u, up, end);
			}

			// re-trace ignoring water this time
			tr = gi.trace (water_start, NULL, NULL, end, self, MASK_SHOT);
		}
	}

	// send gun puff / flash
	if (!((tr.surface) && (tr.surface->flags & SURF_SKY)))
	{
		if (tr.fraction < 1.0)
		{
			if (tr.ent->takedamage)
			{
				T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_BULLET, mod);
			}
			else
			{
				if (strncmp (tr.surface->name, "sky", 3) != 0)
				{
					gi.WriteByte (svc_temp_entity);
					gi.WriteByte (te_impact);
					gi.WritePosition (tr.endpos);
					gi.WriteDir (tr.plane.normal);
					gi.multicast (tr.endpos, MULTICAST_PVS);

					if (self->client)
						PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
				}
			}
		}
	}

	// if went through water, determine where the end and make a bubble trail
	if (water)
	{
		vec3_t	pos;

		VectorSubtract (tr.endpos, water_start, dir);
		VectorNormalize (dir);
		VectorMA (tr.endpos, -2, dir, pos);
		if (gi.pointcontents (pos) & MASK_WATER)
			VectorCopy (pos, tr.endpos);
		else
			tr = gi.trace (pos, NULL, NULL, water_start, tr.ent, MASK_WATER);

		VectorAdd (water_start, tr.endpos, pos);
		VectorScale (pos, 0.5, pos);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BUBBLETRAIL);
		gi.WritePosition (water_start);
		gi.WritePosition (tr.endpos);
		gi.multicast (pos, MULTICAST_PVS);
	}
}


/*
=================
fire_bullet

Fires a single round.  Used for machinegun and chaingun.  Would be fine for
pistols, rifles, etc....
=================
*/
void fire_bullet (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int mod)
{
	fire_lead (self, start, aimdir, damage, kick, TE_GUNSHOT, hspread, vspread, mod);
}


/*
=================
fire_shotgun

Shoots shotgun pellets.  Used by shotgun and super shotgun.
=================
*/
void fire_shotgun (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int count, int mod)
{
	int		i;

	for (i = 0; i < count; i++)
		fire_lead (self, start, aimdir, damage, kick, TE_SHOTGUN, hspread, vspread, mod);
}


/*
=================
fire_blaster

Fires a single blaster bolt.  Used by the blaster and hyper blaster.
=================
*/
void blaster_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	int		mod;

	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	if (other->takedamage)
	{
		if (self->spawnflags & 1)
			mod = MOD_HYPERBLASTER;
		else
			mod = MOD_BLASTER;
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, self->dmg, 1, DAMAGE_ENERGY, mod);
	}
	else
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BLASTER);
		gi.WritePosition (self->s.origin);
		if (!plane)
			gi.WriteDir (vec3_origin);
		else
			gi.WriteDir (plane->normal);
		gi.multicast (self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict (self);
}

void fire_blaster (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int effect, qboolean hyper)
{
	edict_t	*bolt;
	trace_t	tr;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->svflags = SVF_DEADMONSTER;
	// yes, I know it looks weird that projectiles are deadmonsters
	// what this means is that when prediction is used against the object
	// (blaster/hyperblaster shots), the player won't be solid clipped against
	// the object.  Right now trying to run into a firing hyperblaster
	// is very jerky since you are predicted 'against' the shots.
	VectorCopy (start, bolt->s.origin);
	VectorCopy (start, bolt->s.old_origin);
	vectoangles (dir, bolt->s.angles);
	VectorScale (dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->s.modelindex = gi.modelindex ("models/objects/laser/tris.md2");
	bolt->s.sound = gi.soundindex ("misc/lasfly.wav");
	bolt->owner = self;
	bolt->touch = blaster_touch;
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	if (hyper)
		bolt->spawnflags = 1;
	gi.linkentity (bolt);

	if (self->client)
		check_dodge (self, bolt->s.origin, dir, speed);

	tr = gi.trace (self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA (bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch (bolt, tr.ent, NULL, NULL);
	}
}	


/*
=================
fire_grenade
=================
*/
static void Grenade_Explode (edict_t *ent)
{
	vec3_t		origin;
	int			mod;

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	//FIXME: if we are onground then raise our Z just a bit since we are a point?
	if (ent->enemy)
	{
		float	points;
		vec3_t	v;
		vec3_t	dir;

		VectorAdd (ent->enemy->mins, ent->enemy->maxs, v);
		VectorMA (ent->enemy->s.origin, 0.5, v, v);
		VectorSubtract (ent->s.origin, v, v);
		points = ent->dmg - 0.5 * VectorLength (v);
		VectorSubtract (ent->enemy->s.origin, ent->s.origin, dir);
		if (ent->spawnflags & 1)
			mod = MOD_HANDGRENADE;
		else
			mod = MOD_GRENADE;
		T_Damage (ent->enemy, ent, ent->owner, dir, ent->s.origin, vec3_origin, (int)points, (int)points, DAMAGE_RADIUS, mod);
	}

	if (ent->spawnflags & 2)
		mod = MOD_HELD_GRENADE;
	else if (ent->spawnflags & 1)
		mod = MOD_HG_SPLASH;
	else
		mod = MOD_G_SPLASH;
	T_RadiusDamage(ent, ent->owner, ent->dmg, ent->enemy, ent->dmg_radius, mod);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION_WATER);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	}
	else
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION);
	}
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}

static void Grenade_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (!other->takedamage)
	{
		if (ent->spawnflags & 1)
		{
			if (random() > 0.5)
				gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
		}
		else
		{
			gi.sound (ent, CHAN_VOICE, gi.soundindex ("weapons/grenlb1b.wav"), 1, ATTN_NORM, 0);
		}
		return;
	}

	ent->enemy = other;
	Grenade_Explode (ent);
}

void fire_grenade (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float timer, float damage_radius)
{
	edict_t	*grenade;
	vec3_t	dir;
	vec3_t	forward, right, up;

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	grenade = G_Spawn();
	VectorCopy (start, grenade->s.origin);
	VectorScale (aimdir, speed, grenade->velocity);
	VectorMA (grenade->velocity, 200 + crandom() * 10.0, up, grenade->velocity);
	VectorMA (grenade->velocity, crandom() * 10.0, right, grenade->velocity);
	VectorSet (grenade->avelocity, 300, 300, 300);
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_SHOT;
	grenade->solid = SOLID_BBOX;
	grenade->s.effects |= EF_GRENADE;
	VectorClear (grenade->mins);
	VectorClear (grenade->maxs);
	grenade->s.modelindex = gi.modelindex ("models/objects/grenade/tris.md2");
	grenade->owner = self;
	grenade->touch = Grenade_Touch;
	grenade->nextthink = level.time + timer;
	grenade->think = Grenade_Explode;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->classname = "grenade";

	gi.linkentity (grenade);
}

void fire_grenade2 (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float timer, float damage_radius, qboolean held)
{
	edict_t	*grenade;
	vec3_t	dir;
	vec3_t	forward, right, up;

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	grenade = G_Spawn();
	VectorCopy (start, grenade->s.origin);
	VectorScale (aimdir, speed, grenade->velocity);
	VectorMA (grenade->velocity, 200 + crandom() * 10.0, up, grenade->velocity);
	VectorMA (grenade->velocity, crandom() * 10.0, right, grenade->velocity);
	VectorSet (grenade->avelocity, 300, 300, 300);
	grenade->movetype = MOVETYPE_BOUNCE;
	grenade->clipmask = MASK_SHOT;
	grenade->solid = SOLID_BBOX;
	grenade->s.effects |= EF_GRENADE;
	VectorClear (grenade->mins);
	VectorClear (grenade->maxs);
	grenade->s.modelindex = gi.modelindex ("models/objects/grenade2/tris.md2");
	grenade->owner = self;
	grenade->touch = Grenade_Touch;
	grenade->nextthink = level.time + timer;
	grenade->think = Grenade_Explode;
	grenade->dmg = damage;
	grenade->dmg_radius = damage_radius;
	grenade->classname = "hgrenade";
	if (held)
		grenade->spawnflags = 3;
	else
		grenade->spawnflags = 1;
	grenade->s.sound = gi.soundindex("weapons/hgrenc1b.wav");

	if (timer <= 0.0)
		Grenade_Explode (grenade);
	else
	{
		gi.sound (self, CHAN_WEAPON, gi.soundindex ("weapons/hgrent1a.wav"), 1, ATTN_NORM, 0);
		gi.linkentity (grenade);
	}
}


/*
=================
fire_rocket
=================
*/
void rocket_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		origin;
	int			n;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	if (other->takedamage)
	{
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 0, 0, MOD_ROCKET);
	}
	else
	{
		// don't throw any debris in net games
		if (!deathmatch->value && !coop->value)
		{
			if ((surf) && !(surf->flags & (SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING)))
			{
				n = rand() % 5;
				while(n--)
					ThrowDebris (ent, "models/objects/debris2/tris.md2", 2, ent->s.origin);
			}
		}
	}

	T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH);

	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}

void fire_rocket (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*rocket;

	rocket = G_Spawn();
	VectorCopy (start, rocket->s.origin);
	VectorCopy (dir, rocket->movedir);
	vectoangles (dir, rocket->s.angles);
	VectorScale (dir, speed, rocket->velocity);
	rocket->movetype = MOVETYPE_FLYMISSILE;
	rocket->clipmask = MASK_SHOT;
	rocket->solid = SOLID_BBOX;
	rocket->s.effects |= EF_ROCKET;
	VectorClear (rocket->mins);
	VectorClear (rocket->maxs);
	rocket->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	rocket->owner = self;
	rocket->touch = rocket_touch;
	rocket->nextthink = level.time + 8000/speed;
	rocket->think = G_FreeEdict;
	rocket->dmg = damage;
	rocket->radius_dmg = radius_damage;
	rocket->dmg_radius = damage_radius;
	rocket->s.sound = gi.soundindex ("weapons/rockfly.wav");
	rocket->classname = "rocket";

	if (self->client)
		check_dodge (self, rocket->s.origin, dir, speed);

	gi.linkentity (rocket);
}


/*
=================
fire_rail
=================
*/
void fire_rail (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick)
{
	vec3_t		from;
	vec3_t		end;
	trace_t		tr;
	edict_t		*ignore;
	int			mask;
	qboolean	water;

	VectorMA (start, 8192, aimdir, end);
	VectorCopy (start, from);
	ignore = self;
	water = false;
	mask = MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA;
	while (ignore)
	{
		tr = gi.trace (from, NULL, NULL, end, ignore, mask);

		if (tr.contents & (CONTENTS_SLIME|CONTENTS_LAVA))
		{
			mask &= ~(CONTENTS_SLIME|CONTENTS_LAVA);
			water = true;
		}
		else
		{
			//ZOID--added so rail goes through SOLID_BBOX entities (gibs, etc)
			if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client) ||
				(tr.ent->solid == SOLID_BBOX))
				ignore = tr.ent;
			else
				ignore = NULL;

			if ((tr.ent != self) && (tr.ent->takedamage))
				T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, 0, MOD_RAILGUN);
		}

		VectorCopy (tr.endpos, from);
	}

	// send gun puff / flash
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_RAILTRAIL);
	gi.WritePosition (start);
	gi.WritePosition (tr.endpos);
	gi.multicast (self->s.origin, MULTICAST_PHS);
//	gi.multicast (start, MULTICAST_PHS);
	if (water)
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_RAILTRAIL);
		gi.WritePosition (start);
		gi.WritePosition (tr.endpos);
		gi.multicast (tr.endpos, MULTICAST_PHS);
	}

	if (self->client)
		PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
}


/*
=================
fire_bfg
=================
*/
void bfg_explode (edict_t *self)
{
	edict_t	*ent;
	float	points;
	vec3_t	v;
	float	dist;

	if (self->s.frame == 0)
	{
		// the BFG effect
		ent = NULL;
		while ((ent = findradius(ent, self->s.origin, self->dmg_radius)) != NULL)
		{
			if (!ent->takedamage)
				continue;
			if (ent == self->owner)
				continue;
			if (!CanDamage (ent, self))
				continue;
			if (!CanDamage (ent, self->owner))
				continue;

			VectorAdd (ent->mins, ent->maxs, v);
			VectorMA (ent->s.origin, 0.5, v, v);
			VectorSubtract (self->s.origin, v, v);
			dist = VectorLength(v);
			points = self->radius_dmg * (1.0 - sqrt(dist/self->dmg_radius));
			if (ent == self->owner)
				points = points * 0.5;

			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_BFG_EXPLOSION);
			gi.WritePosition (ent->s.origin);
			gi.multicast (ent->s.origin, MULTICAST_PHS);
			T_Damage (ent, self, self->owner, self->velocity, ent->s.origin, vec3_origin, (int)points, 0, DAMAGE_ENERGY, MOD_BFG_EFFECT);
		}
	}

	self->nextthink = level.time + FRAMETIME;
	self->s.frame++;
	if (self->s.frame == 5)
		self->think = G_FreeEdict;
}

void bfg_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (self);
		return;
	}

	if (self->owner->client)
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);

	// core explosion - prevents firing it into the wall/floor
	if (other->takedamage)
		T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, 200, 0, 0, MOD_BFG_BLAST);
	T_RadiusDamage(self, self->owner, 200, other, 100, MOD_BFG_BLAST);

	gi.sound (self, CHAN_VOICE, gi.soundindex ("weapons/bfg__x1b.wav"), 1, ATTN_NORM, 0);
	self->solid = SOLID_NOT;
	self->touch = NULL;
	VectorMA (self->s.origin, -1 * FRAMETIME, self->velocity, self->s.origin);
	VectorClear (self->velocity);
	self->s.modelindex = gi.modelindex ("sprites/s_bfg3.sp2");
	self->s.frame = 0;
	self->s.sound = 0;
	self->s.effects &= ~EF_ANIM_ALLFAST;
	self->think = bfg_explode;
	self->nextthink = level.time + FRAMETIME;
	self->enemy = other;

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BFG_BIGEXPLOSION);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PVS);
}


void bfg_think (edict_t *self)
{
	edict_t	*ent;
	edict_t	*ignore;
	vec3_t	point;
	vec3_t	dir;
	vec3_t	start;
	vec3_t	end;
	int		dmg;
	trace_t	tr;

	if (deathmatch->value)
		dmg = 5;
	else
		dmg = 10;

	ent = NULL;
	while ((ent = findradius(ent, self->s.origin, 256)) != NULL)
	{
		if (ent == self)
			continue;

		if (ent == self->owner)
			continue;

		if (!ent->takedamage)
			continue;

		if (!(ent->svflags & SVF_MONSTER) && (!ent->client) && (strcmp(ent->classname, "misc_explobox") != 0))
			continue;

		VectorMA (ent->absmin, 0.5, ent->size, point);

		VectorSubtract (point, self->s.origin, dir);
		VectorNormalize (dir);

		ignore = self;
		VectorCopy (self->s.origin, start);
		VectorMA (start, 2048, dir, end);
		while(1)
		{
			tr = gi.trace (start, NULL, NULL, end, ignore, CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_DEADMONSTER);

			if (!tr.ent)
				break;

			// hurt it if we can
			if ((tr.ent->takedamage) && !(tr.ent->flags & FL_IMMUNE_LASER) && (tr.ent != self->owner))
				T_Damage (tr.ent, self, self->owner, dir, tr.endpos, vec3_origin, dmg, 1, DAMAGE_ENERGY, MOD_BFG_LASER);

			// if we hit something that's not a monster or player we're done
			if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
			{
				gi.WriteByte (svc_temp_entity);
				gi.WriteByte (TE_LASER_SPARKS);
				gi.WriteByte (4);
				gi.WritePosition (tr.endpos);
				gi.WriteDir (tr.plane.normal);
				gi.WriteByte (self->s.skinnum);
				gi.multicast (tr.endpos, MULTICAST_PVS);
				break;
			}

			ignore = tr.ent;
			VectorCopy (tr.endpos, start);
		}

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_BFG_LASER);
		gi.WritePosition (self->s.origin);
		gi.WritePosition (tr.endpos);
		gi.multicast (self->s.origin, MULTICAST_PHS);
	}

	self->nextthink = level.time + FRAMETIME;
}
void fire_bfg (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius)
{
	edict_t	*bfg;

	bfg = G_Spawn();
	VectorCopy (start, bfg->s.origin);
	VectorCopy (dir, bfg->movedir);
	vectoangles (dir, bfg->s.angles);
	VectorScale (dir, speed, bfg->velocity);
	bfg->movetype = MOVETYPE_FLYMISSILE;
	bfg->clipmask = MASK_SHOT;
	bfg->solid = SOLID_BBOX;
	bfg->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (bfg->mins);
	VectorClear (bfg->maxs);
	bfg->s.modelindex = gi.modelindex ("sprites/s_bfg1.sp2");
	bfg->owner = self;
	bfg->touch = bfg_touch;
	bfg->nextthink = level.time + 8000/speed;
	bfg->think = G_FreeEdict;
	bfg->radius_dmg = 0;
	bfg->dmg_radius = 0;
	bfg->classname = "bfg blast";
	bfg->s.sound = gi.soundindex ("weapons/bfg__l1a.wav");

	bfg->think = bfg_think;
	bfg->nextthink = level.time + FRAMETIME;
	bfg->teammaster = bfg;
	bfg->teamchain = NULL;

	if (self->client)
		check_dodge (self, bfg->s.origin, dir, speed);

	gi.linkentity (bfg);
}
/*
	All of the stuff that I will add for the magic combination mod (at least all of the combining stuff)
*/
void Mix_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point){}
void Magic_Fire_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		dir, point;
	vec3_t		forward, right;
	vec3_t		start;
	vec3_t		offset;
	vec3_t		origin;
	int		mod;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	if (other->takedamage && other->magicType != 5)
	{
		if (ent->spawnflags & 1)
			mod = MOD_HYPERBLASTER;
		else
			mod = MOD_BLASTER;
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 1, DAMAGE_ENERGY, MOD_R_SPLASH);
	}
	if (other->magicType == 5)
	{
		AngleVectors (ent->owner->client->v_angle, forward, right, NULL);
		VectorSet(offset, 24, 8, ent->owner->viewheight-8);
		VectorAdd (offset, vec3_origin, offset);
		P_ProjectSource (ent->owner->client, ent->owner->s.origin, offset, forward, right, start);

		Magic_Combo_Fire (ent->owner, start, forward,500, 200);
	}
	else
	{	
		gi.WriteByte (svc_temp_entity);
		if (ent->waterlevel)
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (origin);
		gi.multicast (ent->s.origin, MULTICAST_PHS);
		
	}

	G_FreeEdict (ent);
}
void Magic_Grab_S_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		p_throw;
	vec3_t		dir, point;
	vec3_t		forward, right;
	vec3_t		start;
	vec3_t		offset;

	vec3_t		origin;
	int			mod;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);
	if(deathmatch)
	{
		if ((other != ent->owner) && !plane)
		{
			AngleVectors (other->client->v_angle, forward, right, NULL);
			VectorSet(offset, 24, 8, ent->viewheight-8);
			VectorAdd (offset, vec3_origin, offset);
			P_ProjectSource (other->client, other->s.origin, offset, forward, right, start);
		
			VectorScale (ent->owner->velocity,1000,p_throw);  
			VectorCopy (p_throw, other->velocity);
		}
	}
	else
	{
		VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
		if (other->takedamage)
		{
			if (ent->spawnflags & 1)
				mod = MOD_HYPERBLASTER;
			else
				mod = MOD_BLASTER;
			T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 10, DAMAGE_ENERGY, mod);
			gi.WriteByte (svc_temp_entity);
		}
	}
	if (other->magicType == 5)
	{
		AngleVectors (ent->owner->client->v_angle, forward, right, NULL);
		VectorSet(offset, 24, 8, ent->owner->viewheight-8);
		VectorAdd (offset, vec3_origin, offset);
		P_ProjectSource (ent->owner->client, ent->owner->s.origin, offset, forward, right, start);

		Magic_Combo_Grab (ent->owner, start, forward, 500);
	}
	else
	{	

		gi.WriteByte (svc_temp_entity);
		if (ent->waterlevel)
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		else
			 gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (origin);
		gi.multicast (ent->s.origin, MULTICAST_PHS);

	}
	G_FreeEdict(ent);
}
void Magic_Heal_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		forward, right;
	vec3_t		start;
	vec3_t		offset;
	vec3_t		origin;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	if (other->takedamage)
	{
		other->health += 30;
	}
	
	if (other->magicType == 5)
	{
		AngleVectors (ent->owner->client->v_angle, forward, right, NULL);
		VectorSet(offset, 24, 8, ent->owner->viewheight-8);
		VectorAdd (offset, vec3_origin, offset);
		P_ProjectSource (ent->owner->client, ent->owner->s.origin, offset, forward, right, start);

		Magic_Combo_Heal (ent->owner, start, forward, 500);
	}
	else
	{	

		gi.WriteByte (svc_temp_entity);
		if (ent->waterlevel)
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		else
			 gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (origin);
		gi.multicast (ent->s.origin, MULTICAST_PHS);

	}

	G_FreeEdict (ent);
}
void Magic_Radial_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		forward, right;
	vec3_t		start;
	vec3_t		offset;
	vec3_t		origin;
	int			n;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	
		// don't throw any debris in net games
	if (!deathmatch->value && !coop->value)
	{
		if ((surf) && !(surf->flags & (SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING)))
		{
			n = rand() % 5;
			while(n--)
				ThrowDebris (ent, "models/objects/debris2/tris.md2", 2, ent->s.origin);
		}
	}
	
	if (other->magicType == 5)
	{
		AngleVectors (ent->owner->client->v_angle, forward, right, NULL);
		VectorSet(offset, 24, 8, ent->owner->viewheight-8);
		VectorAdd (offset, vec3_origin, offset);
		P_ProjectSource (ent->owner->client, ent->owner->s.origin, offset, forward, right, start);

		Magic_Combo_Radial (ent->owner, start, forward, 500,2000,200);
	}
	else
	{	
		gi.WriteByte (svc_temp_entity);
		if (ent->waterlevel)
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (origin);
		gi.multicast (ent->s.origin, MULTICAST_PHS);
		T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH);
	}
	G_FreeEdict (ent);
}
void Magic_MixS_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == ent->owner)
	{
		return;
	}

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	
	G_FreeEdict (ent);
}

void Magic_MixF_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		forward, right;
	vec3_t		start;
	vec3_t		offset;
	vec3_t		origin;
	int			n;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	
		// don't throw any debris in net games
	if (!deathmatch->value && !coop->value)
	{
		if ((surf) && !(surf->flags & (SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING)))
		{
			n = rand() % 5;
			while(n--)
				ThrowDebris (ent, "models/objects/debris2/tris.md2", 2, ent->s.origin);
		}
	}
	
	if (other->magicType == 5)
	{
		AngleVectors (ent->owner->client->v_angle, forward, right, NULL);
		VectorSet(offset, 24, 8, ent->owner->viewheight-8);
		VectorAdd (offset, vec3_origin, offset);
		P_ProjectSource (ent->owner->client, ent->owner->s.origin, offset, forward, right, start);

		Magic_Combo_Nuke (ent->owner, start, forward, 2, 999999, 999999, 999999);
	}
	else
	{	
		if(other->magicType == 14)
		{
			G_FreeEdict(other); //NUKE KILLA
		}
		gi.WriteByte (svc_temp_entity);
		if (ent->waterlevel)
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (origin);
		gi.multicast (ent->s.origin, MULTICAST_PHS);
		T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH);
	}
	G_FreeEdict (ent);
}

void Combo_Fire_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	origin;
	int		mod;

	if (other->magicType == 4)
	{
		gi.cprintf (ent->owner,PRINT_HIGH,"Can Change Spell\n");
	}
	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	if (other->takedamage)
	{
		if (ent->spawnflags & 1)
			mod = MOD_HYPERBLASTER;
		else
			mod = MOD_BLASTER;
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 1, DAMAGE_ENERGY, mod);
		gi.WriteByte (svc_temp_entity);
		if (ent->waterlevel)
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		else
			 gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (origin);
		gi.multicast (ent->s.origin, MULTICAST_PHS);
	}
	else
	{	

		gi.WriteByte (svc_temp_entity);
		if (ent->waterlevel)
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		else
			 gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (origin);
		gi.multicast (ent->s.origin, MULTICAST_PHS);

	}

	G_FreeEdict (ent);
}
void Combo_Grab_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		p_throw;
	vec3_t		dir, point;
	vec3_t		forward, right;
	vec3_t		start;
	vec3_t		offset;

	vec3_t		origin;
	int			mod;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);
	if(deathmatch)
	{
		if ((other != ent->owner) && !plane)
		{
			AngleVectors (other->client->v_angle, forward, right, NULL);
			VectorSet(offset, 24, 8, ent->viewheight-8);
			VectorAdd (offset, vec3_origin, offset);
			P_ProjectSource (other->client, other->s.origin, offset, forward, right, start);
		
			VectorScale (ent->owner->velocity,3000,p_throw);  
			VectorCopy (p_throw, other->velocity);
		}
		else
		{
			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_BLASTER);
			gi.WritePosition (ent->s.origin);
			if (!plane)
				gi.WriteDir (vec3_origin);
			else
				gi.WriteDir (plane->normal);
			gi.multicast (ent->s.origin, MULTICAST_PVS);
		}
	}
	else
	{
		VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
		if (other->takedamage)
		{
			if (ent->spawnflags & 1)
				mod = MOD_HYPERBLASTER;
			else
				mod = MOD_BLASTER;
			T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 10, DAMAGE_ENERGY, mod);
			gi.WriteByte (svc_temp_entity);
			if (ent->waterlevel)
				gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
			else
				 gi.WriteByte (TE_ROCKET_EXPLOSION);
			gi.WritePosition (origin);
			gi.multicast (ent->s.origin, MULTICAST_PHS);
		}
		else
		{
			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_BLASTER);
			gi.WritePosition (ent->s.origin);
			if (!plane)
				gi.WriteDir (vec3_origin);
			else
				gi.WriteDir (plane->normal);
			gi.multicast (ent->s.origin, MULTICAST_PVS);
		}
	}

	G_FreeEdict(ent);
}
void Combo_Heal_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	origin;
	int		mod;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	if (other->takedamage)
	{
		other->health += 100;
		gi.WriteByte (svc_temp_entity);
		if (ent->waterlevel)
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		else
			 gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (origin);
		gi.multicast (ent->s.origin, MULTICAST_PHS);
	}
	else
	{	

		gi.WriteByte (svc_temp_entity);
		if (ent->waterlevel)
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
		else
			 gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (origin);
		gi.multicast (ent->s.origin, MULTICAST_PHS);

	}

	G_FreeEdict (ent);
}
void Combo_Rad_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t	origin;
	int		n;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	if (!deathmatch->value && !coop->value)
	{
		if ((surf) && !(surf->flags & (SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING)))
		{
			n = rand() % 5;
			while(n--)
				ThrowDebris (ent, "models/objects/debris2/tris.md2", 2, ent->s.origin);
		}
	}

	T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH);

	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);
	
	G_FreeEdict (ent);
}
void Combo_Nuke_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t		origin;
	int			n;

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	if (ent->owner->client)
		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	// calculate position for the explosion entity
	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	if (other->takedamage)
	{
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, ent->dmg, 0, 0, MOD_ROCKET);
	}
	else
	{
		// don't throw any debris in net games
		if (!deathmatch->value && !coop->value)
		{
			if ((surf) && !(surf->flags & (SURF_WARP|SURF_TRANS33|SURF_TRANS66|SURF_FLOWING)))
			{
				n = rand() % 5;
				while(n--)
					ThrowDebris (ent, "models/objects/debris2/tris.md2", 2, ent->s.origin);
			}
		}
	}

	T_RadiusDamage(ent, ent->owner, ent->radius_dmg, other, ent->dmg_radius, MOD_R_SPLASH);

	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	else
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}



void Magic_Slow_Fire (edict_t *self, vec3_t start, vec3_t dir, int speed, int damage)
{
	edict_t	*fastFire;

	fastFire = G_Spawn();
	VectorCopy (start, fastFire->s.origin);
	VectorCopy (dir, fastFire->movedir);
	vectoangles (dir, fastFire->s.angles);
	VectorScale (dir, speed, fastFire->velocity);
	fastFire->movetype = MOVETYPE_FLYMISSILE;
	fastFire->clipmask = MASK_SHOT;
	fastFire->solid = SOLID_BBOX;
	fastFire->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (fastFire->mins);
	VectorClear (fastFire->maxs);
	fastFire->s.modelindex = gi.modelindex ("sprites/s_bfg1.sp2");
	fastFire->owner = self;
	fastFire->touch = Magic_Fire_Touch;
	fastFire->nextthink = level.time + 8000/speed;
	fastFire->think = G_FreeEdict;
	fastFire->dmg = damage;
	fastFire->s.sound = gi.soundindex ("weapons/rockfly.wav");
	fastFire->magicType = 1; //basic lvl1 spell

	if (self->client)
		check_dodge (self, fastFire->s.origin, dir, speed);

	gi.linkentity (fastFire);
}
void Magic_Slow_Grab (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	edict_t	*slowGrab;

	slowGrab = G_Spawn();
	VectorCopy (start, slowGrab->s.origin);
	VectorCopy (dir, slowGrab->movedir);
	vectoangles (dir, slowGrab->s.angles);
	VectorScale (dir, speed, slowGrab->velocity);
	slowGrab->movetype = MOVETYPE_FLYMISSILE;
	slowGrab->clipmask = MASK_SHOT;
	slowGrab->solid = SOLID_BBOX;
	slowGrab->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
		VectorClear (slowGrab->mins);
	VectorClear (slowGrab->maxs);
	slowGrab->s.modelindex = gi.modelindex ("sprites/s_bfg1.sp2");
	slowGrab->owner = self;
	slowGrab->touch = Magic_Grab_S_Touch;
	slowGrab->nextthink = level.time + 8000/speed;
	slowGrab->think = G_FreeEdict;
	slowGrab->s.sound = gi.soundindex ("weapons/rockfly.wav");
	slowGrab->magicType = 2;

	if (self->client)
		check_dodge (self, slowGrab->s.origin, dir, speed);

	gi.linkentity (slowGrab);
}
void Magic_Slow_Heal (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	edict_t	*slowHeal;

	slowHeal = G_Spawn();
	VectorCopy (start, slowHeal->s.origin);
	VectorCopy (dir, slowHeal->movedir);
	vectoangles (dir, slowHeal->s.angles);
	VectorScale (dir, speed, slowHeal->velocity);
	slowHeal->movetype = MOVETYPE_FLYMISSILE;
	slowHeal->clipmask = MASK_SHOT;
	slowHeal->solid = SOLID_BBOX;
	slowHeal->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (slowHeal->mins);
	VectorClear (slowHeal->maxs);
	slowHeal->s.modelindex = gi.modelindex ("sprites/s_bfg1.sp2");
	slowHeal->owner = self;
	slowHeal->touch = Magic_Heal_Touch;
	slowHeal->nextthink = level.time + 8000/speed;
	slowHeal->think = G_FreeEdict;

	slowHeal->s.sound = gi.soundindex ("weapons/rockfly.wav");
	slowHeal->magicType = 3;

	if (self->client)
		check_dodge (self, slowHeal->s.origin, dir, speed);

	gi.linkentity (slowHeal);
}
void Magic_Slow_Radial (edict_t *self, vec3_t start, vec3_t dir, int speed, int damage_radius, int radius_damage)
{
	edict_t	*slowRadial;

	slowRadial = G_Spawn();
	VectorCopy (start, slowRadial->s.origin);
	VectorCopy (dir, slowRadial->movedir);
	vectoangles (dir, slowRadial->s.angles);
	VectorScale (dir, speed, slowRadial->velocity);
	slowRadial->movetype = MOVETYPE_FLYMISSILE;
	slowRadial->clipmask = MASK_SHOT;
	slowRadial->solid = SOLID_BBOX;
	slowRadial->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (slowRadial->mins);
	VectorClear (slowRadial->maxs);
	slowRadial->s.modelindex = gi.modelindex ("sprites/s_bfg1.sp2");
	slowRadial->owner = self;
	slowRadial->touch = Magic_Radial_Touch;
	slowRadial->nextthink = level.time + 8000/speed;
	slowRadial->think = G_FreeEdict;
	slowRadial->dmg_radius = damage_radius;
	slowRadial->radius_dmg = radius_damage; 
	slowRadial->s.sound = gi.soundindex ("weapons/rockfly.wav");
	slowRadial->magicType = 4;

	if (self->client)
		check_dodge (self, slowRadial->s.origin, dir, speed);

	gi.linkentity (slowRadial);
}
void Magic_Slow_Mix (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	edict_t	*slowMix;

	slowMix = G_Spawn();
	VectorCopy (start, slowMix->s.origin);
	VectorCopy (dir, slowMix->movedir);
	VectorSet(slowMix->mins, -20, -20, -20);
	VectorSet(slowMix->maxs, 20, 20, 20);
	vectoangles (dir, slowMix->s.angles);
	VectorScale (dir, speed, slowMix->velocity);
	slowMix->movetype = MOVETYPE_FLYMISSILE;
	slowMix->clipmask = MASK_SHOT;
	slowMix->solid = SOLID_BBOX;
	slowMix->takedamage = DAMAGE_YES;
	slowMix->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	//VectorClear (slowMix->mins);
	//VectorClear (slowMix->maxs);
	slowMix->s.modelindex = gi.modelindex ("sprites/s_bfg1.sp2");
	slowMix->owner = self;
	slowMix->touch = Magic_MixS_Touch;
	slowMix->nextthink = level.time + 8000/speed;
	slowMix->think = G_FreeEdict;
	//slowMix->s.sound = gi.soundindex ("weapons/rockfly.wav");
	slowMix->magicType = 5;
	if (self->client)
		check_dodge (self, slowMix->s.origin, dir, speed);

	gi.linkentity (slowMix);
}
void Magic_Fast_Fire (edict_t *self, vec3_t start, vec3_t dir, int speed, int damage)
{
	edict_t	*fastFire;

	fastFire = G_Spawn();
	VectorCopy (start, fastFire->s.origin);
	VectorCopy (dir, fastFire->movedir);
	vectoangles (dir, fastFire->s.angles);
	VectorScale (dir, speed, fastFire->velocity);
	
	fastFire->movetype = MOVETYPE_FLYMISSILE;
	fastFire->clipmask = MASK_SHOT;
	fastFire->solid = SOLID_BBOX;
	fastFire->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (fastFire->mins);
	VectorClear (fastFire->maxs);
	fastFire->s.modelindex = gi.modelindex ("sprites/s_bfg1.sp2");
	fastFire->owner = self;
	fastFire->touch = Magic_Fire_Touch;
	fastFire->nextthink = level.time + 8000/speed;
	fastFire->think = G_FreeEdict;
	fastFire->dmg = damage;
	fastFire->s.sound = gi.soundindex ("models/objects/rocket/tris.md2");
	fastFire->magicType = 5; //basic lvl1 spell

	if (self->client)
		check_dodge (self, fastFire->s.origin, dir, speed);

	gi.linkentity (fastFire);
}
void Magic_Fast_Grab (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	edict_t	*fastGrab;

	fastGrab = G_Spawn();
	VectorCopy (start, fastGrab->s.origin);
	VectorCopy (dir, fastGrab->movedir);
	vectoangles (dir, fastGrab->s.angles);
	VectorScale (dir, speed, fastGrab->velocity);
	fastGrab->movetype = MOVETYPE_FLYMISSILE;
	fastGrab->clipmask = MASK_SHOT;
	fastGrab->solid = SOLID_BBOX;
	fastGrab->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (fastGrab->mins);
	VectorClear (fastGrab->maxs);
	fastGrab->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	fastGrab->owner = self;
	fastGrab->touch = Magic_Grab_S_Touch;
	fastGrab->nextthink = level.time + 8000/speed;
	fastGrab->think = G_FreeEdict;
	fastGrab->s.sound = gi.soundindex ("weapons/rockfly.wav");
	fastGrab->magicType = 6;

	if (self->client)
		check_dodge (self, fastGrab->s.origin, dir, speed);

	gi.linkentity (fastGrab);
}
void Magic_Fast_Heal (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	edict_t	*fastHeal;

	fastHeal = G_Spawn();
	VectorCopy (start, fastHeal->s.origin);
	VectorCopy (dir, fastHeal->movedir);
	vectoangles (dir, fastHeal->s.angles);
	VectorScale (dir, speed, fastHeal->velocity);
	fastHeal->movetype = MOVETYPE_FLYMISSILE;
	fastHeal->clipmask = MASK_SHOT;
	fastHeal->solid = SOLID_BBOX;
	fastHeal->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (fastHeal->mins);
	VectorClear (fastHeal->maxs);
	fastHeal->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	fastHeal->owner = self;
	fastHeal->touch = Magic_Heal_Touch;
	fastHeal->nextthink = level.time + 8000/speed;
	fastHeal->think = G_FreeEdict;

	fastHeal->s.sound = gi.soundindex ("weapons/rockfly.wav");
	fastHeal->magicType = 7;

	if (self->client)
		check_dodge (self, fastHeal->s.origin, dir, speed);

	gi.linkentity (fastHeal);
}
void Magic_Fast_Radial (edict_t *self, vec3_t start, vec3_t dir, int speed, int damage_radius, int radius_damage)
{
	edict_t	*fastRadial;

	fastRadial = G_Spawn();
	VectorCopy (start, fastRadial->s.origin);
	VectorCopy (dir, fastRadial->movedir);
	vectoangles (dir, fastRadial->s.angles);
	VectorScale (dir, speed, fastRadial->velocity);
	fastRadial->movetype = MOVETYPE_FLYMISSILE;
	fastRadial->clipmask = MASK_SHOT;
	fastRadial->solid = SOLID_BBOX;
	fastRadial->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (fastRadial->mins);
	VectorClear (fastRadial->maxs);
	fastRadial->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	fastRadial->owner = self;
	fastRadial->touch = Magic_Radial_Touch;
	fastRadial->nextthink = level.time + 8000/speed;
	fastRadial->think = G_FreeEdict;
	fastRadial->dmg_radius = damage_radius;
	fastRadial->radius_dmg = radius_damage; 
	fastRadial->s.sound = gi.soundindex ("weapons/rockfly.wav");
	fastRadial->magicType = 8;

	if (self->client)
		check_dodge (self, fastRadial->s.origin, dir, speed);

	gi.linkentity (fastRadial);
}
void Magic_Fast_Mix (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	edict_t	*fastMix;

	fastMix = G_Spawn();
	VectorCopy (start, fastMix->s.origin);
	VectorCopy (dir, fastMix->movedir);
	vectoangles (dir, fastMix->s.angles);
	VectorScale (dir, speed, fastMix->velocity);
	fastMix->movetype = MOVETYPE_FLYMISSILE;
	fastMix->clipmask = MASK_SHOT;
	fastMix->solid = SOLID_BBOX;
	fastMix->takedamage = DAMAGE_YES;
	fastMix->mass = 2;
	//fastMix->monsterinfo.aiflags = AI_NOSTEP;
	fastMix->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (fastMix->mins);
	VectorClear (fastMix->maxs);
	fastMix->s.modelindex = gi.modelindex ("models/objects/rocket/tris.md2");
	fastMix->owner = self;
	fastMix->touch = Magic_MixF_Touch;
	fastMix->nextthink = level.time + 8000/speed;
	fastMix->think = G_FreeEdict;
	fastMix->s.sound = gi.soundindex ("weapons/rockfly.wav");
	fastMix->magicType = 9;

	if (self->client)
		check_dodge (self, fastMix->s.origin, dir, speed);

	gi.linkentity (fastMix);
}

void Magic_Combo_Fire (edict_t *self, vec3_t start, vec3_t dir, int speed, int damage)
{
	edict_t	*comboFire;

	comboFire = G_Spawn();
	VectorCopy (start, comboFire->s.origin);
	VectorCopy (dir, comboFire->movedir);
	vectoangles (dir, comboFire->s.angles);
	VectorScale (dir, speed, comboFire->velocity);
	comboFire->movetype = MOVETYPE_FLYMISSILE;
	comboFire->clipmask = MASK_SHOT;
	comboFire->solid = SOLID_BBOX;
	comboFire->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (comboFire->mins);
	VectorClear (comboFire->maxs);
	comboFire->s.modelindex = gi.modelindex ("models/objects/grenade/tris.md2");
	comboFire->owner = self;
	comboFire->touch = Combo_Fire_Touch;
	comboFire->nextthink = level.time + 8000/speed;
	comboFire->think = G_FreeEdict;
	comboFire->dmg = damage;
	comboFire->s.sound = gi.soundindex ("weapons/rockfly.wav");
	comboFire->magicType = 1; //basic lvl1 spell

	if (self->client)
		check_dodge (self, comboFire->s.origin, dir, speed);

	gi.linkentity (comboFire);
}
void Magic_Combo_Grab (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	edict_t	*comboGrab;

	comboGrab = G_Spawn();
	VectorCopy (start, comboGrab->s.origin);
	VectorCopy (dir, comboGrab->movedir);
	vectoangles (dir, comboGrab->s.angles);
	VectorScale (dir, speed, comboGrab->velocity);
	comboGrab->movetype = MOVETYPE_FLYMISSILE;
	comboGrab->clipmask = MASK_SHOT;
	comboGrab->solid = SOLID_BBOX;
	comboGrab->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (comboGrab->mins);
	VectorClear (comboGrab->maxs);
	comboGrab->s.modelindex = gi.modelindex ("models/objects/grenade/tris.md2");
	comboGrab->owner = self;
	comboGrab->touch = Combo_Grab_Touch;
	comboGrab->nextthink = level.time + 8000/speed;
	comboGrab->think = G_FreeEdict;
	comboGrab->s.sound = gi.soundindex ("weapons/rockfly.wav");
	comboGrab->magicType = 11;

	if (self->client)
		check_dodge (self, comboGrab->s.origin, dir, speed);

	gi.linkentity (comboGrab);
}
void Magic_Combo_Heal (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	edict_t	*comboHeal;

	comboHeal = G_Spawn(); 
	VectorCopy (start, comboHeal->s.origin);
	VectorCopy (dir, comboHeal->movedir);
	vectoangles (dir, comboHeal->s.angles);
	VectorScale (dir, speed, comboHeal->velocity);
	comboHeal->movetype = MOVETYPE_FLYMISSILE;
	comboHeal->clipmask = MASK_SHOT;
	comboHeal->solid = SOLID_BBOX;
	comboHeal->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (comboHeal->mins);
	VectorClear (comboHeal->maxs);
	comboHeal->s.modelindex = gi.modelindex ("models/objects/grenade/tris.md2");
	comboHeal->owner = self;
	comboHeal->touch = Combo_Heal_Touch;
	comboHeal->nextthink = level.time + 8000/speed;
	comboHeal->think = G_FreeEdict;

	comboHeal->s.sound = gi.soundindex ("weapons/rockfly.wav");
	comboHeal->magicType = 12;

	if (self->client)
		check_dodge (self, comboHeal->s.origin, dir, speed);

	gi.linkentity (comboHeal);
}
void Magic_Combo_Radial (edict_t *self, vec3_t start, vec3_t dir, int speed, int damage_radius, int radius_damage)
{
	edict_t	*comboRad;

	comboRad = G_Spawn();
	VectorCopy (start, comboRad->s.origin);
	VectorCopy (dir, comboRad->movedir);
	vectoangles (dir, comboRad->s.angles);
	VectorScale (dir, speed, comboRad->velocity);
	comboRad->movetype = MOVETYPE_FLYMISSILE;
	comboRad->clipmask = MASK_SHOT;
	comboRad->solid = SOLID_BBOX;
	comboRad->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (comboRad->mins);
	VectorClear (comboRad->maxs);
	comboRad->s.modelindex = gi.modelindex ("models/objects/grenade/tris.md2");
	comboRad->owner = self;
	comboRad->touch = Combo_Rad_Touch;
	comboRad->nextthink = level.time + 8000/speed;
	comboRad->think = G_FreeEdict;
	comboRad->s.sound = gi.soundindex ("weapons/rockfly.wav");
	comboRad->dmg_radius = damage_radius;
	comboRad->radius_dmg = radius_damage;
	comboRad->magicType = 13;

	if (self->client)
		check_dodge (self, comboRad->s.origin, dir, speed);

	gi.linkentity (comboRad);
}
void Magic_Combo_Nuke (edict_t *self, vec3_t start, vec3_t dir, int speed, int damage, int damage_radius, int radius_damage)
{
	edict_t	*Nuke;

	Nuke = G_Spawn();
	VectorCopy (start, Nuke->s.origin);
	VectorCopy (dir, Nuke->movedir);
	vectoangles (dir, Nuke->s.angles);
	VectorScale (dir, speed, Nuke->velocity);
	Nuke->movetype = MOVETYPE_FLYMISSILE;
	Nuke->clipmask = MASK_ALL;
	Nuke->solid = SOLID_TRIGGER;
	Nuke->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
	VectorClear (Nuke->mins);
	VectorClear (Nuke->maxs);
	Nuke->s.modelindex = gi.modelindex ("models/objects/grenade/tris.md2");
	Nuke->owner = self;
	Nuke->touch = Combo_Nuke_Touch;
	Nuke->dmg = damage;
	Nuke->dmg_radius = damage_radius;
	Nuke->radius_dmg = radius_damage;
	Nuke->nextthink = level.time + 8000/speed;
	Nuke->think = G_FreeEdict;
	Nuke->s.sound = gi.soundindex ("weapons/rockfly.wav");
	Nuke->magicType = 14;

	if (self->client)
		check_dodge (self, Nuke->s.origin, dir, speed);

	gi.linkentity (Nuke);
}
/*
	End of all of the crazy shoot pickles I'm adding
*/