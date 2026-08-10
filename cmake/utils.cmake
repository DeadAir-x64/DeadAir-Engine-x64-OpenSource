include_guard()

function(target_sources_grouped)
    cmake_parse_arguments(
        PARSED_ARGS
        ""
        "TARGET;NAME;SCOPE"
        "FILES"
        ${ARGN}
    )

    if(NOT PARSED_ARGS_TARGET)
        message(FATAL_ERROR "You must provide a target name")
    endif()

    if(NOT PARSED_ARGS_NAME)
        message(FATAL_ERROR "You must provide a source group name")
    endif()

    if(NOT PARSED_ARGS_SCOPE)
        set(PARSED_ARGS_SCOPE PRIVATE)
    endif()

    target_sources(${PARSED_ARGS_TARGET} ${PARSED_ARGS_SCOPE} ${PARSED_ARGS_FILES})

    source_group(${PARSED_ARGS_NAME} FILES ${PARSED_ARGS_FILES})
endfunction()

# [DA_PORT] Отпечаток ревизии обязан попадать в бинарник, иначе лог краша не с чем сопоставить.
#
# Прежняя версия звала `git` просто по имени и глушила ошибку через ERROR_QUIET. Наши сборочные
# скрипты выставляют PATH="/c/msys64/mingw64/bin:/usr/bin:/bin", а git туда не входит — поэтому во
# ВСЕХ отгруженных сборках строка выглядела как `commit[] branch[]`, то есть пустой. Обнаружилось на
# логе тестера: краш в xrGame по смещению, а сопоставить не с чем — двоичные файлы того дня
# отличались друг от друга, и какой из них у него, узнать было нельзя.
#
# Теперь: git ищется через find_package (полный путь, PATH не нужен), а если его нет вовсе — в
# бинарник идёт явная метка, а не пустота. Отдельно помечается «грязное» дерево: сборка с
# незакоммиченными правками невоспроизводима, и знать об этом надо сразу.
#
# Непроиндексированные файлы намеренно игнорируются (--untracked-files=no): в рабочем дереве их
# всегда много, и они на содержимое сборки не влияют.
function(query_git_info output_sha output_branch)
    find_package(Git QUIET)

    if(NOT GIT_EXECUTABLE)
        message(WARNING "[DA_PORT] git не найден: ревизия в сборку не попадёт, логи крашей будет не с чем сопоставить")
        set(${output_sha} "no-git" PARENT_SCOPE)
        set(${output_branch} "no-git" PARENT_SCOPE)
        return()
    endif()

    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 --verify HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_SHA1
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_BRANCH
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_DIRTY
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT GIT_SHA1)
        set(GIT_SHA1 "unknown")
    elseif(GIT_DIRTY)
        set(GIT_SHA1 "${GIT_SHA1}-dirty")
    endif()

    if(NOT GIT_BRANCH)
        set(GIT_BRANCH "unknown")
    endif()

    set(${output_sha} ${GIT_SHA1} PARENT_SCOPE)
    set(${output_branch} ${GIT_BRANCH} PARENT_SCOPE)
endfunction()

function(calculate_xray_build_id output)
    set(XRAY_START_DAY   31)
    set(XRAY_START_MONTH 1)
    set(XRAY_START_YEAR  1999)

    set(DAYS_IN_MONTH 0 31 28 31 30 31 30 31 31 30 31 30 31) # first is dummy

    # Acquire timestamp in "date month year" format
    string(TIMESTAMP current_date "%d %m %Y")

    # Transform string into a list, then extract 3 separate variables
    string(REPLACE " " ";" current_date_list ${current_date})
    list(GET current_date_list 0 CURRENT_DATE_DAY)
    list(GET current_date_list 1 CURRENT_DATE_MONTH)
    list(GET current_date_list 2 CURRENT_DATE_YEAR)

    # Check if current date is before the start date
    # See https://github.com/OpenXRay/xray-16/issues/1611
    if ( (CURRENT_DATE_YEAR LESS XRAY_START_YEAR)
        OR ( (CURRENT_DATE_YEAR EQUAL XRAY_START_YEAR)
            AND (CURRENT_DATE_MONTH LESS XRAY_START_MONTH)
            OR ( (CURRENT_DATE_MONTH EQUAL XRAY_START_MONTH)
                AND (CURRENT_DATE_DAY LESS XRAY_START_DAY) ) ) )
        set(${output} 0 PARENT_SCOPE)
        return()
    endif()

    # Calculate XRAY build ID
    math(EXPR build_id "(${CURRENT_DATE_YEAR} - ${XRAY_START_YEAR}) * 365 + ${CURRENT_DATE_DAY} - ${XRAY_START_DAY}")

    set(it 1)
    while(it LESS CURRENT_DATE_MONTH)
        list(GET DAYS_IN_MONTH ${it} days)
        math(EXPR build_id "${build_id} + ${days}")

        math(EXPR it "${it} + 1")
    endwhile()

    set(it 1)
    while(it LESS XRAY_START_MONTH)
        list(GET DAYS_IN_MONTH ${it} days)
        math(EXPR build_id "${build_id} - ${days}")

        math(EXPR it "${it} + 1")
    endwhile()

    # Set requested variable
    set(${output} ${build_id} PARENT_SCOPE)
endfunction()
