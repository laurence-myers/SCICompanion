;;; Sierra Script 1.0 - (do not remove this comment)
(script# 956)
(include sci.sh)

(public
	d1DeadBreak 0
	d1TwoDeadBreaks 1
	d1DeadBreakAfterExit 2
)

(local
	local0
	local1
	local2
	local3
)
(procedure (d1DeadBreak)
	(while (and local0 local1)
		(= local1 1)
		(= local2 2)
	)
)

(procedure (d1TwoDeadBreaks)
	(while (and local0 local1)
		(= local1 1)
		(= local2 2)
	)
)

(procedure (d1DeadBreakAfterExit)
	(repeat
		(if local0
			(if local1 (= local1 1) else (break))
		else
			(= local2 2)
		)
		(= local3 3)
	)
)
