from pymongo import MongoClient
import csv

file_path = 'training_data.csv'
header = ['meanAx', 'sdAx', 'rangeAx', 'meanAy', 'sdAy', 'rangeAy', 'meanAz', 'sdAz', 'rangeAz', 'meanGx', 'sdGx', 'rangeGx', 'meanGy', 'sdGy', 'rangeGy', 'meanGz', 'sdGz', 'rangeGz', 'label', 'studentId']
 
uri = "mongodb+srv://shivaan:shivaan@assessmentcluster.xyr5ml6.mongodb.net/?retryWrites=true&w=majority&appName=AssessmentCluster"
client = MongoClient(uri)
db = client["imu_db"]
acce = db["imu_data"]

#Change the filter to whatever you're trying to find (e.g. "data.9" : "right")
filter = {}

results = acce.find(filter)
    
with open(file_path, 'w', newline='') as csvfile:
    writer = csv.writer(csvfile)
    writer.writerows([header])
    for doc in results:
       writer.writerows([doc['data']])
    print('Data has been written to CSV file.')
    
