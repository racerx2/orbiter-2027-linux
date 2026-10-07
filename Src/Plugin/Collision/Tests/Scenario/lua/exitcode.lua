-- not upstream: Scn.ExitCode, the script ends the process with code 3 at frame 5
return {step = function(k) if k == 5 then oapi.exit(3) end end}
