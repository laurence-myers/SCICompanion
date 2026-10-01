;;; Sierra Script 1.0 - (do not remove this comment)
(script# 964)
(include sci.sh)

; Two exported procedures. The test moves export 1 into the code of the
; first one, as Sierra left some stale exports (QfG3 script 7).
(public
	staleFirst 0
	staleSecond 1
)

(local
	local0
	local1
)

(procedure (staleFirst)
	(= local0 1)
	(= local1 2)
	(= local0 3)
)

(procedure (staleSecond)
	(= local1 4)
)
