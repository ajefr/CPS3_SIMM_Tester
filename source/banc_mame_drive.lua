-- Pilote le BIOS de test dans MAME : suit un scenario (attendre un texte,
-- appuyer sur des boutons), ecrit le texte de chaque ecran dans screens.txt
-- et injecte des pannes sur les lectures des flashs (variable FAULTS).
local cpu = manager.machine.devices[":maincpu"]
local prog = cpu.spaces["program"]
local out = assert(io.open(os.getenv("SCREENS") or "screens.txt", "w"))
local scen = os.getenv("SCENARIO") or "ident"
local faults = os.getenv("FAULTS") or ""

local function screen()
	local lines = {}
	for y = 0, 27 do
		local t = {}
		for x = 0, 47 do
			local v = prog:read_u32(0x05040000 + (y * 64 + x) * 4)
			local ch = (v >> 16) & 0xff
			if ch < 32 or ch > 126 then ch = 32 end
			t[#t + 1] = string.char(ch)
		end
		lines[#lines + 1] = (table.concat(t):gsub("%s+$", ""))
	end
	return table.concat(lines, "\n")
end

local function dump(tag)
	out:write("==== ", tag, " (frame ", manager.machine.screens[":screen"]:frame_number(), ")\n", screen(), "\n")
	out:flush()
	manager.machine.video:snapshot()
end

-- RANDRAM=1 : RAM video aleatoires comme sur une vraie carte a l'allumage
if os.getenv("RANDRAM") == "1" then
	math.randomseed(1234)
	for a = 0x04080000, 0x040bfffc, 4 do prog:write_u32(a, math.random(0, 0xffffffff)) end
	for a = 0x05040000, 0x0504fffc, 4 do prog:write_u32(a, math.random(0, 0xffffffff)) end
	for a = 0x04000000, 0x0407fffc, 4 do prog:write_u32(a, math.random(0, 0xffffffff)) end
end

-- SSLOSS=p : une ecriture sur p en RAM SS est corrompue (simule la vraie carte)
local ssloss = tonumber(os.getenv("SSLOSS") or "0")
if ssloss > 0 then
	sstap = prog:install_write_tap(0x05040000, 0x0504ffff, "ssloss", function(off, data, mask)
		if math.random() < ssloss then return math.random(0, 0xffffffff) end
		return data
	end)
end

-- pannes ------------------------------------------------------------
local bank = 0
local guard = false
local taps = {}
taps[#taps + 1] = prog:install_write_tap(0x040c0088, 0x040c008b, "bank", function(off, data, mask)
	if mask & 0xffff0000 ~= 0 then bank = ((data >> 16) & 0xffff) - 2 end
end)
-- gfx : "g<simm>.<puce>:d<bit>=<0|1>", "g<simm>.<puce>:a<k>" ou "g<simm>.<puce>:b<paire>" (conflit de bus)
-- prog : "p<simm>.<puce>:d<bit>=<0|1>"
local gf, pf = {}, {}
for f in faults:gmatch("[^,]+") do
	local kind, s, c, rest = f:match("^([gp])(%d)%.(%d):(.+)$")
	s = tonumber(s); c = tonumber(c)
	local b, v = rest:match("^d(%d)=(%d)$")
	local a = rest:match("^a(%d+)$")
	local bq = rest:match("^b(%d)$")
	local e = { s = s, c = c, bit = b and tonumber(b), val = v and tonumber(v), a = a and tonumber(a), dead = rest == "x", bus = bq and tonumber(bq) }
	if kind == "g" then gf[#gf + 1] = e else pf[#pf + 1] = e end
end
-- INTEL=<slot> : simule des puces type Intel 28F016 (ID 89/A0) dans une
-- SIMM graphique : 9090 -> mode ID, FFFF -> lecture ; en mode ID les
-- lectures de la paire rendent 89/A0 (lecture du contenu inchangee).
local isl = tonumber(os.getenv("INTEL") or "0")
local imode = {}
if isl >= 3 then
	taps[#taps + 1] = prog:install_write_tap(0x04200000, 0x043fffff, "iw", function(off, data, mask)
		local s = 3 + bank // 8
		if s ~= isl then return end
		local pair = (bank % 8) // 2
		local v = (mask & 0xffff0000 ~= 0) and ((data >> 16) & 0xffff) or (data & 0xffff)
		if v == 0x9090 then imode[pair] = "id" elseif v == 0xffff then imode[pair] = nil end
	end)
	taps[#taps + 1] = prog:install_read_tap(0x04200000, 0x043fffff, "ir", function(off, data, mask)
		local s = 3 + bank // 8
		if s ~= isl then return data end
		local pair = (bank % 8) // 2
		if imode[pair] == "id" then return 0x8989a0a0 end
		return data
	end)
end
if #gf > 0 then
	taps[#taps + 1] = prog:install_read_tap(0x04200000, 0x043fffff, "gfault", function(off, data, mask)
		if guard then return data end
		local s = 3 + bank // 8
		local pair = (bank % 8) // 2
		for _, e in ipairs(gf) do
			if e.s == s and e.c // 2 == pair then
				local lanes = (e.c % 2 == 1) and 0x00ff00ff or 0xff00ff00
				if e.bus then
					-- conflit de bus : la paire e.bus repond aussi (ET des donnees)
					local oldb = bank
					local half = bank % 2
					guard = true
					prog:write_u16(0x040c0088, (s - 3) * 8 + e.bus * 2 + half + 2)
					local o = prog:read_u32(off)
					prog:write_u16(0x040c0088, oldb + 2)
					guard = false
					data = (data & ~lanes) | (data & o & lanes)
				elseif e.dead then
					data = data | lanes
				elseif e.bit then
					local m = 0
					for sh = 0, 24, 8 do
						if (lanes >> sh) & 0xff ~= 0 then m = m | ((1 << e.bit) << sh) end
					end
					if e.val == 0 then data = data & ~m else data = data | m end
				elseif e.a then
					-- ligne d'adresse Ak collee a 0 (k >= 1 : bit k-1 du mot)
					local wbit = 1 << (e.a - 1)
					local o = (off - 0x04200000) // 4
					if o & wbit ~= 0 then
						guard = true
						local alt = prog:read_u32(off - wbit * 4)
						guard = false
						data = (data & ~lanes) | (alt & lanes)
					end
				end
			end
		end
		return data
	end)
end
if #pf > 0 then
	taps[#taps + 1] = prog:install_read_tap(0x06000000, 0x06ffffff, "pfault", function(off, data, mask)
		local s = off >= 0x06800000 and 2 or 1
		for _, e in ipairs(pf) do
			if e.s == s then
				local m = (1 << e.bit) << (24 - 8 * e.c)
				if e.val == 0 then data = data & ~m else data = data | m end
			end
		end
		return data
	end)
end

-- scenarios -----------------------------------------------------------
local inp = manager.machine.ioport.ports[":INPUTS"]
local F = {
	U = inp.fields["P1 Up"], D = inp.fields["P1 Down"], L = inp.fields["P1 Left"],
	R = inp.fields["P1 Right"], ["1"] = inp.fields["P1 Jab Punch"],
	["2"] = inp.fields["P1 Strong Punch"], ["3"] = inp.fields["P1 Fierce Punch"],
}
-- etape : {attendre = "texte", touches = "DD1", nom = "capture"}
local S = {
	ident = {
		{ w = "> 1. Identification", k = "", n = "menu" },
		{ w = "> 1. Identification", k = "1" },
		{ w = "Identification des puces", k = "", n = "ident" },
	},
	crc3 = {
		{ w = "> 1. Identification", k = "D1" },
		{ w = "<  SIMM 3", k = "1" },
		{ w = "Toutes les puces|Puce%(s%)|Aucun dump", k = "", n = "crc3" },
	},
	slot = {
		{ w = "> 1. Identification", k = "D1" },
		{ w = "<  SIMM 3", k = "", n = "slot3" },
		{ w = "<  SIMM 3", k = "RRR" },
		{ w = "<  SIMM 6", k = "", n = "slot6" },
	},
	crc1 = {
		{ w = "> 1. Identification", k = "D1" },
		{ w = "<  SIMM 3", k = "LL1" },
		{ w = "Toutes les puces|Puce%(s%)|Aucun dump", k = "", n = "crc1" },
	},
	cmp34 = {
		{ w = "> 1. Identification", k = "DD1" },
		{ w = "<  SIMM 2", k = "R1" },
		{ w = "<  SIMM 4", k = "1" },
		{ w = "puce%(s%) en defaut|Aucune erreur", k = "", n = "cmp" },
		{ w = "Droite/gauche", k = "RRRRRR", n = nil },
		{ w = "Detail SIMM3.5", k = "", n = "cmp_detail5" },
	},
	cmp34b = {
		{ w = "> 1. Identification", k = "DD1" },
		{ w = "<  SIMM 2", k = "R1" },
		{ w = "<  SIMM 4", k = "1" },
		{ w = "puce%(s%) en defaut|Aucune erreur", k = "", n = "cmp" },
		{ w = "Droite/gauche", k = "RRR", n = nil },
		{ w = "Detail SIMM3.2", k = "", n = "cmp_detail2" },
	},
	crc6 = {
		{ w = "> 1. Identification", k = "D1" },
		{ w = "<  SIMM 3", k = "RRR1" },
		{ w = "Toutes les puces|Puce%(s%)|Aucun dump", k = "", n = "crc6" },
	},
	cmp21 = {
		{ w = "> 1. Identification", k = "", n = "menu" },
		{ w = "> 1. Identification", k = "DD1" },
		{ w = "<  SIMM 2", k = "1" },
		{ w = "<  SIMM 1", k = "1" },
		{ w = "puce%(s%) en defaut|Aucune erreur", k = "", n = "cmp21" },
		{ w = "Droite/gauche", k = "RRR" },
		{ w = "Detail SIMM2.2", k = "", n = "cmp21_detail2" },
	},
	write3 = {
		{ w = "> 1. Identification", k = "DDD1" },
		{ w = "<  SIMM 3", k = "1" },
		{ w = "CONFIRMER", k = "3" },
		{ w = "puce%(s%) en defaut|Aucune erreur", k = "", n = "write3" },
		{ w = "Droite/gauche", k = "RRRRRR" },
		{ w = "Detail SIMM3.5", k = "", n = "write3_detail5" },
	},
	write2 = {
		{ w = "> 1. Identification", k = "DDD1" },
		{ w = "<  SIMM 3", k = "L1" },
		{ w = "CONFIRMER", k = "3" },
		{ w = "puce%(s%) en defaut|Aucune erreur", k = "", n = "write2" },
		{ w = "Droite/gauche", k = "RRR" },
		{ w = "Detail SIMM2.2", k = "", n = "write2_detail2" },
	},
}
local steps = S[scen]
-- BIOS anglais (LANGUE=en) : memes scenarios, textes attendus traduits
if os.getenv("LANGUE") == "en" then
	local tr = {
		["> 1. Identification"] = "> 1. Chip identification",
		["Identification des puces"] = "Chip identification",
		["Toutes les puces|Puce%(s%)|Aucun dump"] = "All chips|UNKNOWN chip|No dump",
		["puce%(s%) en defaut|Aucune erreur"] = "bad chip%(s%)|No errors",
		["Droite/gauche"] = "Right/left",
		["CONFIRMER"] = "CONFIRM",
		["<  SIMM 3"] = "<  SIMM 3", ["<  SIMM 6"] = "<  SIMM 6",
	}
	for _, st in ipairs(steps) do st.w = tr[st.w] or st.w end
end
local idx, queue, hold, wait = 1, {}, 0, 0
local frame = 0

local function matches(txt, pat)
	for p in pat:gmatch("[^|]+") do
		if txt:find(p) then return true end
	end
	return false
end

emu.register_frame_done(function()
	frame = frame + 1
	for _, f in pairs(F) do f:set_value(0) end
	if hold > 0 then
		F[queue[1]]:set_value(1)
		hold = hold - 1
		if hold == 0 then table.remove(queue, 1); wait = 8 end
		return
	end
	if wait > 0 then wait = wait - 1; return end
	if #queue > 0 then hold = 4; return end
	if frame % 10 ~= 0 then return end
	local st = steps[idx]
	if not st then
		manager.machine:exit()
		return
	end
	local txt = screen()
	if matches(txt, st.w) then
		if st.n then dump(st.n) end
		for ch in st.k:gmatch(".") do queue[#queue + 1] = ch end
		idx = idx + 1
		wait = 20
	end
	if frame % 3000 == 0 then dump("attente " .. st.w) end
end)
keep_taps = taps
