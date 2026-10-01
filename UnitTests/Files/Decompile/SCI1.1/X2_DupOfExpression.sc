;;; Sierra Script 1.0 - (do not remove this comment)
(script# 955)
(include sci.sh)
(use System)

; A dup of a value that is no plain value (a sum): Sierra's optimiser makes a
; dup only of a number, a variable or a property, so the scope engine
; refuses it. The classic engine repeats the expression:
; (Random (+ param1 param2) (+ param1 param2)). With auto, the classic
; engine gives the function.
(public
	x2DupOfExpression 0
)

(procedure (x2DupOfExpression param1 param2)
	(asm
		push2
		lsp param1
		lap param2
		add
		push
		dup
		callk Random, 4
		ret
	)
)
