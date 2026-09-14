;;; Sierra Script 1.0 - (do not remove this comment)
(script# 935)
(include sci.sh)

; The indexer of an indexed compound assignment is evaluated twice (Sierra's
; sequence). An indexer with a side effect gets a warning; a plain expression
; does not.
(public
	c5IndexerSideEffect 0
)

(procedure (c5IndexerSideEffect param1 &tmp [temp0 4])
	(+= [temp0 (+ param1 1)] 1)
	(+= [temp0 (++ param1)] 1)
)
