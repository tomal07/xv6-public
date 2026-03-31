#define CHECKMARK "\xE2\x9C\x85"
#define XMARK "\xE2\x9D\x8C"
#define REDQUESTIONMARK "\xE2\x9D\x93"
#define PARTYPOPPER "\xF0\x9F\x8E\x89"

#define TEST(test_func)           do{                                                                                               \
                                    (test_func)();                                                                                  \
                                    printf(1, CHECKMARK " " #test_func "\n");                                                       \
                                  } while(0)

#define PRINT_ERROR(fmt, args...) do{                                                                                               \
                                    printf(2, XMARK " %s\n", __FUNCTION__);                                                         \
                                    printf(2, REDQUESTIONMARK " " fmt "\n", ##args);                                                \
                                  } while(0)

#define ASSERT(cond)              do{                                                                                               \
                                    if(!(cond)){                                                                                    \
                                      PRINT_ERROR("ERROR in line %d - '%s' is false", __LINE__, #cond);                             \
                                      exit();                                                                                       \
                                    }                                                                                               \
                                  } while(0)

// Used for child processes to hang if an error occurred, that way when waiting for the child it will just hang.
#define ASSERT_HANG(cond)         do{                                                                                               \
                                    if(!(cond)){                                                                                    \
                                      PRINT_ERROR("ERROR in line %d - '%s' is false, hanging...", __LINE__, #cond);                 \
                                      for(;;){}                                                                                     \
                                    }                                                                                               \
                                  } while(0)
