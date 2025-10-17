
from pymongo.mongo_client import MongoClient

uri = "mongodb+srv://shivaan:shivaan@assessmentcluster.xyr5ml6.mongodb.net/?retryWrites=true&w=majority&appName=AssessmentCluster"
client = MongoClient(uri)

# print all databases on this connected cluster
databases = client.list_database_names()
print("All avaliable databases: ")
for db in databases:
    print(db)

# get the database named "sample_mflix"
mflix_db = client['sample_mflix']

# list all collections in this database
collections = mflix_db.list_collection_names()
print("All collections in the database:")
for c in collections:
    print(c)
