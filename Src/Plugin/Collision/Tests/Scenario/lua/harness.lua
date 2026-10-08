-- not upstream: state dump and actions of the collision addon scenario tests (Design CA E4 7.1, 7.4, design-C-T 4.1), called by CollTestHarness each post-step
local H = {}

local function g(x) return string.format('%.17g', x) end
local function vec(v) return g(v.x) .. ',' .. g(v.y) .. ',' .. g(v.z) end
local function mat(m)
	return table.concat({g(m.m11), g(m.m12), g(m.m13), g(m.m21), g(m.m22), g(m.m23), g(m.m31), g(m.m32), g(m.m33)}, ',')
end
local function name_of(h)
	if h == nil then return '-' end
	return oapi.get_objname(h) or '?'
end

local function docked(v)
	local out = {}
	for i = 0, v:get_dockcount() - 1 do
		local d = v:get_dockhandle(i)
		local o = d and v:get_dockstatus(d)
		if o then out[#out + 1] = i .. ':' .. name_of(o) end
	end
	if #out == 0 then return '-' end
	return table.concat(out, ';')
end

local function parent(v)
	for i = 0, v:get_attachmentcount(true) - 1 do
		local a = v:get_attachmenthandle(true, i)
		local o = a and v:get_attachmentstatus(a)
		if o then return name_of(o) end
	end
	return '-'
end

function H.dump(f, k) -- frame k: the committed state at SimT0 of frame k (T 1.6)
	f:write('F ', k, ' ', g(oapi.get_simtime()), ' ', g(oapi.get_simmjd()), ' ', g(oapi.get_tacc()), '\n')
	local refs, order = {}, {}
	for i = 0, vessel.get_count() - 1 do
		local v = vessel.get_interface(i)
		local ref = v:get_gravityref()
		local rn = name_of(ref)
		if ref and not refs[rn] then refs[rn] = ref; order[#order + 1] = rn end
		f:write('V ', v:get_name(), ' ', v:get_classname() or '-', ' fs=', v:get_flightstatus(), ' m=', g(v:get_mass()),
			' p=', vec(v:get_globalpos()), ' v=', vec(v:get_globalvel()), ' w=', vec(v:get_angvel()),
			' R=', mat(v:get_rotationmatrix()), ' I=', vec(v:get_pmi()), ' ref=', rn, ' dk=', docked(v), ' par=', parent(v), '\n')
	end
	for _, rn in ipairs(order) do
		local h = refs[rn]
		f:write('B ', rn, ' p=', vec(oapi.get_globalpos(h)), ' v=', vec(oapi.get_globalvel(h)), '\n')
	end
end

local function act(k, a) -- harness actions run in the frame hook of frame k, after the addon's post-step (E4 7.4)
	local v = a.v ~= '' and vessel.get_interface(a.v) or nil
	if a.kind == 'LUASAVE' then
		oapi.savescenario('Tests/Coll/Saved/' .. a.v, 'collision test save')
	elseif a.kind == 'LUASHOT' then -- the back buffer as PNG: the frame rendered at the end of frame k-1 (T 5.2)
		oapi.save_surface('Images/' .. a.v, nil, IMAGEFORMAT.PNG)
	elseif v == nil then
		oapi.write_log('CollTestHarness act k=' .. k .. ' ' .. a.kind .. ' ' .. a.v .. ' failed: no such vessel')
		return
	elseif a.kind == 'LUACALL' then
		local fn = v[a.m]
		if fn == nil then
			oapi.write_log('CollTestHarness act k=' .. k .. ' LUACALL ' .. a.v .. ' ' .. a.m .. ' failed: no such method')
			return
		end
		fn(v, unpack(a.args))
	elseif a.kind == 'LUASTATUS' then
		v:defset_status(v:get_status(2))
	end
	oapi.write_log('CollTestHarness act k=' .. k .. ' simt=' .. g(oapi.get_simtime()) .. ' ' .. a.kind .. ' ' .. a.v .. (a.m ~= '' and (' ' .. a.m) or ''))
end

function H.start(c) -- clbkSimulationStart of CollTestHarness
	H.c, H.k, H.T = c, 0, {}
	if c.dump then
		H.f = io.open('TestOut/state.dump', 'w')
		H.f:write('# not upstream: scenario state dump v1\n')
		H.f:write('H scn=', c.scn, ' h=', c.h, ' version=', g(oapi.get_orbiter_version()), ' run=', c.run, '\n')
	end
	if c.test ~= '' then H.T = dofile('./Script/Tests/Coll/' .. c.test .. '.lua') or {} end
	for _, a in ipairs(c.actions) do
		if a.kind == 'LUASHOT' then -- screenshots without the menu and info bars (T 5.2)
			oapi.set_mainmenuvisibilitymode(1)
			oapi.set_maininfovisibilitymode(1)
			break
		end
	end
	if c.script ~= '' then -- the scenario's own script as a coroutine: proc.skip yields; loadfile, as dofile is a C call a yield cannot cross
		local chunk = assert(loadfile('./Script/' .. c.script .. '.lua'))
		H.co = coroutine.create(chunk)
	end
end

function H.fail(msg)
	if H.f then H.f:write('FAIL ', H.k, ' ', msg, '\n'); H.f:close(); H.f = nil end
	error('harness: ' .. msg)
end

local function resume(co)
	local ok, e = coroutine.resume(co)
	if not ok then oapi.write_log('CollTestHarness script error: ' .. tostring(e)) end
end

function H.frame() -- frame k = the k-th post-step, after every plugin listed before CollTestHarness (E4 7.2)
	local c = H.c
	H.k = H.k + 1
	local k = H.k
	if k <= c.frames then
		if H.f and (k == 1 or k % c.every == 0) then H.dump(H.f, k) end
		if H.T.step then H.T.step(k, H) end
		for _, a in ipairs(c.actions) do
			if a.k == k then act(k, a) end
		end
		if k == c.frames and H.f then
			H.f:write('END ', c.frames, '\n')
			H.f:close()
			H.f = nil
		end
	end
	if H.co and coroutine.status(H.co) == 'suspended' then resume(H.co) end -- one cycle per frame, as the scenario's interpreter
	for i = 1, branch.nslot do -- branches the scenario script started (proc.bg), resumed as proc.skip does
		if branch[i] ~= nil and coroutine.status(branch[i]) == 'suspended' then resume(branch[i]) end
	end
end

function H.stop() -- clbkSimulationEnd: the world may be gone, files only
	if H.f then H.f:close(); H.f = nil end
end

return H
