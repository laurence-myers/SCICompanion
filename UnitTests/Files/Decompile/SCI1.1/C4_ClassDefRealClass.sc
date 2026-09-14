;;; Sierra Script 1.0 - (do not remove this comment)
(script# 934)
(include sci.sh)
(use System)

; A classdef that names the species of a class the game has (0 is Object).
; The selectors of a send to it are checked as that class: this script must
; not compile, because Object has no method "c4BogusSelector".
(public
	c4ClassDefRealClass 0
)

(classdef C4Alias
	class# 0
	(properties)
	(methods)
)

(procedure (c4ClassDefRealClass)
	(C4Alias c4BogusSelector:)
)
