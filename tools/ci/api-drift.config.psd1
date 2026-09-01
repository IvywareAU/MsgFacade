@{
    # ---------------------------------------------------------------------------------
    #  MsgFacade: which of the vtable facade the COM server actually carries.
    #
    #  Read by check_api_drift.ps1. The upstream here is MsgFacade.h -- the ONE public
    #  header, pure-vtable, and by its own design rules the only thing a client of this
    #  repository includes. The downstream is MsgcoreCom.idl, the dual-interface server
    #  built on top of it.
    #
    #  THIS PAIR IS WORTH MORE THAN THE MSGCORE ONE, because unlike a C ABI over a C++
    #  library -- which is a deliberate subset and always will be -- MsgFacade.h states a
    #  1:1 intent out loud: "each method here maps 1:1 onto a [dual] interface method
    #  (LPCWSTR->BSTR, void*+size->SAFEARRAY(VT_UI1))". A stated 1:1 is a promise a check
    #  can hold to account. Every line this check reports is either a method the server
    #  does not carry, or a promise that needs rewording.
    #
    #  TypeMap below is transcribed from MsgcoreCom.idl's own header comment, which
    #  already writes the correspondence down. Transcribed rather than parsed out of that
    #  comment on purpose: a comment that drives a check silently stops being a comment
    #  and starts being code nobody knows is executable. If the two disagree, that is a
    #  finding -- fix whichever is wrong, in the open.
    #
    #  TWO DELIBERATE ASYMMETRIES, and both are mapped rather than omitted:
    #
    #    * IMsgLibrary has no COM face at all. CoCreateInstance(MsgStore) replaces the
    #      factory -- the IDL says MsgStore is "the one creatable object". It is mapped
    #      to a name that does not exist so the absence lands in the allowlist WITH A
    #      REASON, rather than in a config comment nobody greps.
    #
    #    * IMsgAttrCom and IMsgDescCom have no upstream type. They are a node's ATTR and
    #      CHILD scopes, which are scopes in the facade and interfaces in COM. This check
    #      runs upstream->downstream only, so it cannot see them, and no allowlist entry
    #      will ever mention them. Said here because an invisible surface is exactly what
    #      this check exists to stop being invisible -- the reverse direction is worth
    #      building the day either interface grows a method of its own.
    # ---------------------------------------------------------------------------------

    AllowFile = 'tools/ci/api-drift.allow'

    Pairs = @(
        @{
            Id = 'facade-to-com'

            # MsgFacade.h only. MsgFacadeFn.hpp is a header-only std::function / RAII
            # convenience layer over these same interfaces -- it binds nothing new, and a
            # COM server has no use for a C++ lambda wrapper.
            Upstream = @{
                Kind  = 'cxx'
                Files = @('include/MsgFacade.h')
            }

            Surface = @{
                Kind  = 'idl'
                Files = @('com/MsgcoreCom.idl')
            }

            TypeMap = @{
                'IMsgStore'        = 'IMsgStoreCom'
                'IMsgNode'         = 'IMsgFieldCom'
                'IMsgCursor'       = 'IMsgCursorCom'
                'IMsgList'         = 'IMsgListCom'
                'IMsgVect'         = 'IMsgVectCom'
                'IMsgWalker'       = 'IMsgRecursCom'
                'IMsgStoreEvents'  = '_IMsgStoreEvents'
                'IMsgPagingEvents' = 'IMsgPagingSink'
                'IMsgLibrary'      = 'IMsgLibraryCom'
            }
        }
    )
}
