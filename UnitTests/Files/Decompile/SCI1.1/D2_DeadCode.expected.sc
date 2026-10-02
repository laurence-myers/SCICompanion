;;; Sierra Script 1.0 - (do not remove this comment)
(script# 958)
(include sci.sh)

(public
	d2DeadAfterJmp 0
	d2DeadAfterBreak 1
)

(local
	local0
	local1
	local2
	local3
)
(procedure (d2DeadAfterJmp)
	(= local1 1)
	(= local3 2)
)

(procedure (d2DeadAfterBreak)
	(while local0
		(if local1 (break) else (= local3 3))
	)
)
