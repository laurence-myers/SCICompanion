;;; Sierra Script 1.0 - (do not remove this comment)
(script# 918)
(include sci.sh)

(public
	f8AssignBeforeCondInRet 0
)

(procedure (f8AssignBeforeCondInRet param1 param2 &tmp [temp0 4] [temp4 4])
	(+= [temp0 param1] param2)
	(return (if (>= [temp0 param1] [temp4 param1]) 1 else 0))
)
