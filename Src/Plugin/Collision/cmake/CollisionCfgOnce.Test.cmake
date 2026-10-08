# not upstream: collision addon, cmake -P -DCOLL_SRC=<addon source dir> -DCOLL_WORK=<scratch dir>: CollisionCfgOnce.cmake copies, upgrades v1 files in place and leaves newer ones alone
cmake_minimum_required(VERSION 3.21)
set(tpl "${COLL_SRC}/Config/Collision.cfg")
set(script "${COLL_SRC}/cmake/CollisionCfgOnce.cmake")
file(REMOVE_RECURSE "${COLL_WORK}")
file(MAKE_DIRECTORY "${COLL_WORK}")
set(fails 0)

function(run dst)
	execute_process(COMMAND ${CMAKE_COMMAND} "-DCOLL_CFG_SRC=${tpl}" "-DCOLL_CFG_DST=${dst}" -P "${script}" RESULT_VARIABLE r OUTPUT_VARIABLE o ERROR_VARIABLE e)
	if (NOT r EQUAL 0)
		message(SEND_ERROR "CfgOnce failed on ${dst}: ${e}")
	endif ()
	set(log "${o}${e}" PARENT_SCOPE)
endfunction()

function(expect name dst want)
	file(READ "${dst}" got)
	if (NOT got STREQUAL want)
		message(SEND_ERROR "${name}: got\n${got}\n---- want\n${want}")
	else ()
		message(STATUS "pass: ${name}")
	endif ()
endfunction()

file(READ "${tpl}" T)
set(head "; not upstream: settings of the collision addon (Modules/Plugin/Collision); read once when a simulation session starts\n")
set(ver "; Version of this file's defaults; the build and install upgrade older files in place and keep every other line\nCollisionCfgVersion = 2\n")

# missing: the template, created with its folder
run("${COLL_WORK}/a/Config/Collision.cfg")
expect("missing file copied" "${COLL_WORK}/a/Config/Collision.cfg" "${T}")

# the shipped v1 file with user edits and the old test keys: CollisionModel 0 -> 1, the version line added, every other line kept
set(body1 "; Collisions and new damage on (1) or off (0); off still loads, saves, shows and repairs damage\n")
set(body2 "\n; FALSE: contacts are detected and logged, no response is written\nCollisionResponse = TRUE\n; CollisionModel = 0\nCollisionLog = 3 ; mine\n[x;y]\nCollisionTestKick =\nCollisionTestModelAt =\ncollisionmodel = 0\n")
file(WRITE "${COLL_WORK}/b.cfg" "${head}${body1}CollisionModel = 0${body2}")
run("${COLL_WORK}/b.cfg")
expect("v1 upgraded" "${COLL_WORK}/b.cfg" "${head}${ver}${body1}CollisionModel = 1${body2}")
if (NOT log MATCHES "CollisionModel = 0 of a version 1 file set to 1")
	message(SEND_ERROR "v1 upgrade did not log the CollisionModel change: ${log}")
endif ()
run("${COLL_WORK}/b.cfg")
expect("upgrade runs once" "${COLL_WORK}/b.cfg" "${head}${ver}${body1}CollisionModel = 1${body2}")
if (log MATCHES "upgraded")
	message(SEND_ERROR "a v2 file was upgraded again: ${log}")
endif ()

# v1 with a lower-case key, CRLF, no header comment: the version line at the top, LF line ends (file(READ) drops CR)
file(WRITE "${COLL_WORK}/c.cfg" "  collisionModel\t=\t0  ; off\r\nCollisionLog = 2\r\n")
run("${COLL_WORK}/c.cfg")
expect("v1 CRLF without header" "${COLL_WORK}/c.cfg" "${ver}  collisionModel\t=\t1  ; off\nCollisionLog = 2\n")
file(WRITE "${COLL_WORK}/d.cfg" "${head}CollisionModel = 1\n")
run("${COLL_WORK}/d.cfg")
expect("v1 with collisions on" "${COLL_WORK}/d.cfg" "${head}${ver}CollisionModel = 1\n")

# a v2 file with a deliberate 0 and a newer file: untouched
file(WRITE "${COLL_WORK}/e.cfg" "${head}${ver}CollisionModel = 0\n")
run("${COLL_WORK}/e.cfg")
expect("v2 keeps a deliberate 0" "${COLL_WORK}/e.cfg" "${head}${ver}CollisionModel = 0\n")
file(WRITE "${COLL_WORK}/f.cfg" "CollisionCfgVersion = 3\nCollisionModel = 0\n")
run("${COLL_WORK}/f.cfg")
expect("newer file untouched" "${COLL_WORK}/f.cfg" "CollisionCfgVersion = 3\nCollisionModel = 0\n")

# a version line with a bad value is replaced in place
file(WRITE "${COLL_WORK}/g.cfg" "${head}CollisionCfgVersion = x\nCollisionModel = 0\n")
run("${COLL_WORK}/g.cfg")
expect("bad version replaced" "${COLL_WORK}/g.cfg" "${head}CollisionCfgVersion = 2\nCollisionModel = 1\n")

# a v1 file with a UTF-8 BOM: upgraded as without it, the BOM kept
string(ASCII 239 187 191 bom)
file(WRITE "${COLL_WORK}/h.cfg" "${bom}${head}CollisionModel = 0\n")
run("${COLL_WORK}/h.cfg")
expect("v1 with a BOM" "${COLL_WORK}/h.cfg" "${bom}${head}${ver}CollisionModel = 1\n")
file(WRITE "${COLL_WORK}/i.cfg" "${bom}CollisionModel = 0\n")
run("${COLL_WORK}/i.cfg")
expect("v1 with a BOM on the key line" "${COLL_WORK}/i.cfg" "${bom}${ver}CollisionModel = 1\n")
